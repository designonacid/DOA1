"""DOA1 sound matching: analyze an audio file and search DOA1 parameters for a preset that sounds close.

The search renders candidates with preset_lab (the real DOA1 engine, effects and arpeggiator compiled
without JUCE), measures them with the same analysis as the target, and keeps what gets closer.

Works best on an isolated one-shot of a single synth sound (one note, up to ~3 s): a pluck, lead note,
bass note, pad swell, stab. Full mixes, drums, vocals and loops with several notes will only give a
rough approximation.
"""
from __future__ import annotations

import json, os, random, subprocess, sys, tempfile, wave
from dataclasses import dataclass, field

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
import make_presets as mp  # noqa: E402  (preset definitions, defaults, tuning)

SR = 48000
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


# ----------------------------------------------------------------------------- audio I/O
def load_audio(path: str) -> np.ndarray:
    """Returns float32 stereo (n, 2) at 48 kHz."""
    try:
        import soundfile as sf
        data, sr = sf.read(path, always_2d=True, dtype="float32")
    except Exception:
        with wave.open(path, "rb") as w:
            sr, ch, sw, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
            raw = w.readframes(n)
        if sw == 2:   data = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
        elif sw == 3:
            b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
            data = ((b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16)) << 8 >> 8).astype(np.float32) / 8388608.0
        elif sw == 4: data = np.frombuffer(raw, dtype="<i4").astype(np.float32) / 2147483648.0
        else: raise ValueError(f"unsupported sample width {sw}")
        data = data.reshape(-1, ch)
    if data.shape[1] == 1: data = np.repeat(data, 2, axis=1)
    data = data[:, :2]
    if sr != SR:
        try:
            from scipy.signal import resample_poly
            from math import gcd
            g = gcd(int(sr), SR)
            data = resample_poly(data, SR // g, int(sr) // g, axis=0).astype(np.float32)
        except Exception:
            t = np.arange(int(len(data) * SR / sr)) * sr / SR
            data = np.stack([np.interp(t, np.arange(len(data)), data[:, c]) for c in range(2)], axis=1).astype(np.float32)
    return data


def write_wav(path: str, x: np.ndarray):
    x = np.clip(x, -1, 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((x * 32767).astype("<i2").tobytes())


# ----------------------------------------------------------------------------- analysis
@dataclass
class Features:
    duration: float
    f0: float | None
    midi: int
    cents: float
    env: np.ndarray            # dB per 10 ms, first 1.5 s, peak = 0
    centroid: np.ndarray       # log2(Hz) per 10 ms, first 1.0 s
    harm_early: np.ndarray     # harmonics 1..16 in dB rel. strongest, 30-130 ms
    harm_late: np.ndarray      # same, 350-450 ms (or latest available)
    noise: float               # 0 = purely harmonic, 1 = noise
    beating: float             # dB fluctuation of harmonics 2-6 (unison / detune)
    width: float               # 1 - L/R correlation
    attack_ms: float
    decay_ms: float            # time from peak to -12 dB
    extras: dict = field(default_factory=dict)


def _frames(m, win, hop):
    n = 1 + max(0, (len(m) - win) // hop)
    idx = np.arange(win)[None, :] + hop * np.arange(n)[:, None]
    return m[idx] if n > 0 else np.zeros((0, win))


def _f0(m: np.ndarray) -> float | None:
    seg = m[int(0.06 * SR): int(0.4 * SR)]
    if len(seg) < 4096: seg = m[: min(len(m), int(0.4 * SR))]
    if len(seg) < 2048 or np.max(np.abs(seg)) < 1e-4: return None
    seg = seg * np.hanning(len(seg))
    n = 1 << int(np.ceil(np.log2(2 * len(seg))))
    spec = np.fft.rfft(seg, n)
    ac = np.fft.irfft(np.abs(spec) ** 2)[: len(seg)]
    ac /= ac[0] + 1e-12
    lo, hi = int(SR / 2000), int(SR / 30)
    region = ac[lo:hi]
    if len(region) < 3: return None
    best = int(np.argmax(region))
    # first peak that is nearly as strong as the best one (avoids octave errors)
    for i in range(1, len(region) - 1):
        if region[i] > region[i - 1] and region[i] >= region[i + 1] and region[i] > 0.88 * region[best]:
            best = i; break
    if region[best] < 0.3: return None
    k = best + lo
    if 1 <= k < len(ac) - 1:   # parabolic interpolation
        a, b, c = ac[k - 1], ac[k], ac[k + 1]
        k = k + 0.5 * (a - c) / (a - 2 * b + c + 1e-12)
    return SR / k


def _harmonics(m, t0, t1, f0):
    seg = m[int(t0 * SR): int(t1 * SR)]
    if f0 is None or len(seg) < 1024: return np.full(16, -60.0)
    n = 8192
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    freqs = np.fft.rfftfreq(n, 1 / SR)
    out = []
    for h in range(1, 17):
        f = h * f0
        if f > SR * 0.45: out.append(-60.0); continue
        band = (freqs > f * 0.97) & (freqs < f * 1.03)
        out.append(20 * np.log10(spec[band].max() + 1e-9) if band.any() else -60.0)
    out = np.array(out); out -= out.max()
    return np.maximum(out, -60.0)


def analyze_array(x: np.ndarray) -> Features:
    m = x.mean(axis=1)
    peak = np.max(np.abs(m))
    if peak < 1e-5: raise ValueError("the audio is silent")
    # trim leading silence, keep at most 3 s
    hop = 240
    rms = np.sqrt(np.mean(_frames(m, hop, hop) ** 2, axis=1) + 1e-12)
    on = int(np.argmax(rms > rms.max() * 10 ** (-40 / 20)))
    start = max(0, on * hop - int(0.005 * SR))
    x = x[start: start + 3 * SR]; m = m[start: start + 3 * SR]
    m = m / (np.max(np.abs(m)) + 1e-12)
    # sounding duration: until the level stays below -50 dB
    fr = np.sqrt(np.mean(_frames(m, 480, 480) ** 2, axis=1) + 1e-12)
    envdb = 20 * np.log10(fr / fr.max())
    above = np.where(envdb > -50)[0]
    duration = float((above[-1] + 1) * 0.01) if len(above) else len(m) / SR
    env = np.full(150, -80.0); k = min(150, len(envdb)); env[:k] = np.maximum(envdb[:k], -80)
    pk = int(np.argmax(envdb))
    a10 = np.where(envdb >= -1)[0]
    attack_ms = float(a10[0] * 10) if len(a10) else 0.0
    after = np.where(envdb[pk:] < -12)[0]
    decay_ms = float(after[0] * 10) if len(after) else duration * 1000
    # spectral centroid per 10 ms
    F = _frames(np.concatenate([m, np.zeros(2048)]), 2048, 480)[:100]
    S = np.abs(np.fft.rfft(F * np.hanning(2048), axis=1)); freqs = np.fft.rfftfreq(2048, 1 / SR)
    cen = (S * freqs).sum(1) / (S.sum(1) + 1e-9)
    loud = np.sqrt((F ** 2).mean(1)) > 1e-3
    centroid = np.where(loud, np.log2(np.maximum(cen, 30)), np.nan)
    c = np.full(100, np.nan); c[: len(centroid)] = centroid
    f0 = _f0(m)
    if f0 and f0 > 0:
        midi_f = 69 + 12 * np.log2(f0 / 440.0); midi = int(round(midi_f)); cents = float((midi_f - midi) * 100)
    else:
        midi, cents = 60, 0.0
    late_t = min(0.35, max(0.13, duration - 0.12))
    he = _harmonics(m, 0.03, 0.13, f0); hl = _harmonics(m, late_t, late_t + 0.1, f0)
    # noisiness: energy away from harmonics, 30-130 ms
    seg = m[int(0.03 * SR): int(0.13 * SR)]
    noise = 1.0
    if len(seg) > 1024:
        spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 8192)) ** 2; fq = np.fft.rfftfreq(8192, 1 / SR)
        band = (fq > 50) & (fq < 10000)
        if f0:
            ratio = fq / f0; near = np.abs(ratio - np.round(ratio)) * f0 < max(15.0, 0.04 * f0)
            noise = float(spec[band & ~near].sum() / (spec[band].sum() + 1e-12))
        else:
            geo = np.exp(np.mean(np.log(spec[band] + 1e-12))); noise = float(min(1.0, geo / (spec[band].mean() + 1e-12) * 3))
    # beating: frame-to-frame fluctuation of harmonics 2-6 between 50 and 600 ms
    beating = 0.0
    if f0:
        hs = [ _harmonics(m, t, t + 0.04, f0)[1:6] for t in np.arange(0.05, min(0.6, duration - 0.05), 0.02) ]
        if len(hs) > 3: beating = float(np.mean(np.std(np.array(hs), axis=0)))
    L, R = x[: SR, 0], x[: SR, 1]
    corr = float(np.dot(L, R) / (np.sqrt(np.dot(L, L) * np.dot(R, R)) + 1e-12))
    return Features(duration, f0, midi, cents, env, c, he, hl, noise, beating, 1 - corr, attack_ms, decay_ms)


def analyze_file(path: str) -> Features:
    return analyze_array(load_audio(path))


def describe(f: Features) -> dict:
    """Plain-language summary for the LLM."""
    h = f.harm_early
    if f.f0 is None: wave_guess = "noise or unpitched"
    elif f.noise > 0.5: wave_guess = "noisy, partly pitched"
    elif h[1] < -30 and h[2] < -20: wave_guess = "sine-like (almost no overtones)"
    elif h[1] < h[2] - 8 and h[3] < h[2] - 8: wave_guess = "square / pulse-like (odd harmonics)"
    elif h[2] < -25: wave_guess = "triangle-like (weak, odd overtones)"
    else: wave_guess = "saw-like (all harmonics)"
    cen = f.centroid[~np.isnan(f.centroid)]
    early = float(2 ** np.mean(cen[:5])) if len(cen) >= 5 else None
    late = float(2 ** np.mean(cen[25:35])) if len(cen) >= 35 else None
    return {
        "note": f"{NOTE_NAMES[f.midi % 12]}{f.midi // 12 - 1}" if f.f0 else "unpitched",
        "f0_hz": round(f.f0, 1) if f.f0 else None,
        "tuning_cents": round(f.cents, 1),
        "duration_s": round(f.duration, 2),
        "attack_ms": round(f.attack_ms), "decay_to_-12dB_ms": round(f.decay_ms),
        "waveform_guess": wave_guess,
        "brightness_start_hz": round(early) if early else None,
        "brightness_after_250ms_hz": round(late) if late else None,
        "filter_movement": None if not (early and late) else ("closes quickly (plucky)" if late < early * 0.6 else "fairly static" if late > early * 0.85 else "closes gently"),
        "noise_amount": round(f.noise, 2),
        "detune_or_movement": round(f.beating, 2),
        "stereo_width": round(f.width, 2),
    }


def distance(a: Features, b: Features) -> float:
    d_env = np.mean(np.abs(np.maximum(a.env, -60) - np.maximum(b.env, -60))) / 10
    both = ~np.isnan(a.centroid) & ~np.isnan(b.centroid)
    wts = np.where(np.arange(100) < 30, 2.0, 1.0)[both]           # the first 300 ms matter most
    d_cen = float(np.sum(wts * np.abs(a.centroid[both] - b.centroid[both])) / np.sum(wts)) if both.any() else 1.0
    d_att = abs(np.log2((a.attack_ms + 5) / (b.attack_ms + 5))) * 0.3
    w_h = 1.0 if (a.f0 and b.f0) else 0.0
    d_h = (np.mean(np.abs(a.harm_early - b.harm_early)) + np.mean(np.abs(a.harm_late - b.harm_late))) / 20
    return float(1.0 * d_env + d_att + 1.2 * d_cen + w_h * d_h + 2.0 * abs(a.noise - b.noise)
                 + 0.15 * abs(a.beating - b.beating) + 1.0 * abs(a.width - b.width))


# ----------------------------------------------------------------------------- rendering
def lab_path() -> str:
    for p in (os.environ.get("DOA1_PRESET_LAB", ""), os.path.join(TOOLS, "preset_lab"), os.path.join(TOOLS, "preset_lab.exe"),
              os.path.join(TOOLS, "..", "build", "preset_lab"), os.path.join(TOOLS, "..", "build", "Release", "preset_lab.exe")):
        if p and os.path.exists(p): return p
    # build it on first use
    out = os.path.join(TOOLS, "preset_lab")
    subprocess.run(["c++", "-O2", "-std=c++17", os.path.join(TOOLS, "preset_lab.cpp"), "-o", out], check=True)
    return out


def factory_presets() -> list[tuple[str, dict]]:
    tuning = {}
    tp = os.path.join(TOOLS, "tuning.json"); lp = os.path.join(TOOLS, "levels.json")
    if os.path.exists(tp): tuning = json.load(open(tp))
    levels = json.load(open(lp)) if os.path.exists(lp) else {}
    out = []
    for n, v in mp.PRESETS:
        out.append((n, mp.full({**v, **tuning.get(n, {})}, levels, n)))
    return out


def render_batch(candidates: list[dict], midi: int, hold: float, tail: float) -> list[np.ndarray]:
    lab = lab_path()
    with tempfile.TemporaryDirectory() as d:
        tsv = os.path.join(d, "c.tsv")
        with open(tsv, "w") as f:
            for i, p in enumerate(candidates):
                f.write(f"c{i:04d}\t" + ",".join(f"{k}={float(x):.6g}" for k, x in p.items()) + "\n")
        env = dict(os.environ, LAB_NOTE=str(midi), LAB_HOLD=f"{hold:.3f}", LAB_TAIL=f"{tail:.3f}")
        subprocess.run([lab, tsv, os.path.join(d, "m.json"), d], check=True, env=env, stdout=subprocess.DEVNULL)
        return [load_audio(os.path.join(d, f"c{i:04d}.wav")) for i in range(len(candidates))]


def prepare(p: dict, cents: float) -> dict:
    """One-shot friendly: no arpeggiator or delay, tuning offset applied to every oscillator."""
    q = dict(p); q["arp_on"] = 0; q["dl_on"] = 0
    for o in mp.OSCS: q[f"{o}_fine"] = float(np.clip(p.get(f"{o}_fine", 0) + cents, -100, 100))
    return q


# ----------------------------------------------------------------------------- search space
SEARCH = [
    ("a1_wave", "choice", [0, 1, 2, 3]), ("a1_voices", "int", 1, 8), ("a1_detune", "float", 0.0, 0.6),
    ("a2_on", "choice", [0, 1]), ("a2_wave", "choice", [0, 1, 2, 3]), ("a2_oct", "int", -2, 2), ("a2_vol", "float", 0.1, 0.8),
    ("b1_on", "choice", [0, 1]), ("b1_wave", "choice", [3, 4, 0, 1]), ("b1_oct", "int", -2, 1), ("b1_vol", "float", 0.05, 0.7),
    ("a_cutoff", "log", 80.0, 20000.0), ("a_reso", "float", 0.0, 0.9), ("a_slope", "choice", [0, 1]), ("a_drive", "float", 0.0, 0.7),
    ("a_key", "float", 0.0, 1.0),
    ("m1_amt", "float", 0.0, 0.9), ("m1_d", "log", 0.02, 2.0), ("m1_s", "float", 0.0, 1.0),
    ("a_a", "log", 0.0005, 1.5), ("a_d", "log", 0.02, 3.0), ("a_s", "float", 0.0, 1.0), ("a_r", "log", 0.01, 2.0),
    ("rv_on", "choice", [0, 1]), ("rv_mix", "float", 0.0, 0.4), ("rv_size", "float", 0.3, 0.95),
    ("ch_on", "choice", [0, 1]), ("ch_mix", "float", 0.0, 0.5),
    ("dist_on", "choice", [0, 1]), ("dist_amt", "float", 0.0, 0.6),
]


def mutate(p: dict, strength: float, rng: random.Random) -> dict:
    """Changes 1-4 parameters; step size shrinks with strength."""
    q = dict(p)
    for key, kind, *r in rng.sample(SEARCH, k=rng.randint(1, 4)):
        if kind == "choice": q[key] = rng.choice(r[0])
        elif kind == "int": q[key] = int(np.clip(round(q.get(key, r[0]) + rng.gauss(0, 1.5 * strength + 0.5)), r[0], r[1]))
        elif kind == "float": q[key] = float(np.clip(q.get(key, r[0]) + rng.gauss(0, (r[1] - r[0]) * 0.3 * strength), r[0], r[1]))
        else:
            lo, hi = np.log(r[0]), np.log(r[1]); v = np.log(max(q.get(key, r[0]), r[0]))
            q[key] = float(np.exp(np.clip(v + rng.gauss(0, (hi - lo) * 0.25 * strength), lo, hi)))
        if key in ("a_a", "a_d", "a_s", "a_r"): q["b" + key[1:]] = q[key]   # keep the two parts' amp envelopes together
        if key in ("a1_voices", "a1_detune"): q["a2" + key[2:]] = q[key]
    if q.get("m1_amt", 0) > 0 and q.get("m1_target", 0) == 0: q["m1_target"] = 1
    q["a1_on"] = 1
    return q


def seed_from_features(f: Features, rng: random.Random) -> list[dict]:
    """Starting points built straight from the analysis (waveform, envelope, filter movement, unison)."""
    d = describe(f)
    p = dict(mp.DEFAULTS)
    g = d["waveform_guess"]
    p["a1_wave"] = 1 if "square" in g else 2 if "triangle" in g else 3 if "sine" in g else 0
    if f.noise > 0.5 or not f.f0:
        p.update({"b1_on": 1, "b1_wave": 4, "b1_vol": 0.6})
    wide = f.width > 0.25 or f.beating > 4.0
    p["a1_voices"] = 6 if wide else 1; p["a1_detune"] = 0.25 if wide else 0.1
    # amp envelope
    p["a_a"] = max(0.0005, f.attack_ms / 1000 * 0.8)
    held = f.env[min(149, int(f.duration * 60))] > -9
    p["a_s"] = 0.8 if held else 0.0
    p["a_d"] = max(0.03, f.decay_ms / 1000 * (1.0 if held else 2.5))
    p["a_r"] = 0.2
    for k in ("a", "d", "s", "r"): p[f"b_{k}"] = p[f"a_{k}"]
    # filter: cutoff near the late brightness, envelope sized by how much it closes
    early, late = d["brightness_start_hz"] or 2000, d["brightness_after_250ms_hz"] or 2000
    base = float(np.clip(late * 1.5, 80, 20000))
    p["a_cutoff"] = base
    if early > late * 1.3:
        p.update({"m1_target": 1, "m1_amt": float(np.clip(np.log2(early / late) / 6 + 0.1, 0.1, 0.9)),
                  "m1_d": max(0.04, f.decay_ms / 1000 * 0.7), "m1_s": 0.0})
    if f.duration > 1.2 and not held: p.update({"rv_on": 1, "rv_mix": 0.15, "rv_size": 0.7})
    p["master"] = -9.0
    seeds = []
    for mul in (0.6, 1.0, 1.7):
        q = dict(p); q["a_cutoff"] = float(np.clip(base * mul, 80, 20000)); seeds.append(q)
    return seeds


@dataclass
class MatchResult:
    params: dict
    distance: float
    start_distance: float
    closest_factory: str
    factory_distance: float
    renders: int
    target: Features
    result: Features
    preview: np.ndarray


def match(target: Features, effort: str = "normal", seed: int = 1, progress=None) -> MatchResult:
    rng = random.Random(seed)
    hold = max(0.15, min(2.5, target.duration * 0.6)); tail = max(0.4, target.duration - hold + 0.3)
    gens, pop = {"quick": (8, 12), "normal": (16, 16), "thorough": (30, 20)}.get(effort, (16, 16))
    fac = factory_presets() + [(f"analysis seed {i + 1}", s) for i, s in enumerate(seed_from_features(target, rng))]
    cands = [prepare(v, target.cents) for _, v in fac]
    audio = render_batch(cands, target.midi, hold, tail)
    scored = []
    for (n, _), c, a in zip(fac, cands, audio):
        try: scored.append((distance(target, analyze_array(a)), n, c))
        except ValueError: pass
    scored.sort(key=lambda t: t[0])
    best_d, best_name, best = scored[0]
    start_d = best_d
    elites = [s[2] for s in scored[:3]]
    renders = len(cands)
    if progress: progress(f"closest factory preset: {best_name} (distance {best_d:.3f})")
    for g in range(gens):
        strength = 1.0 - 0.75 * g / max(1, gens - 1)
        kids = [mutate(rng.choice(elites) if rng.random() < 0.3 else best, strength, rng) for _ in range(pop)]
        audio = render_batch(kids, target.midi, hold, tail); renders += len(kids)
        res = []
        for k, a in zip(kids, audio):
            try: res.append((distance(target, analyze_array(a)), k))
            except ValueError: pass
        res.sort(key=lambda t: t[0])
        if res and res[0][0] < best_d: best_d, best = res[0]
        elites = [best] + [r[1] for r in res[:2]]
        if progress: progress(f"generation {g + 1}/{gens}: distance {best_d:.3f}")
    # final render and level: peak at -3 dBFS
    a = render_batch([best], target.midi, hold, tail)[0]
    peak = float(np.max(np.abs(a)) + 1e-9)
    best = dict(best); best["master"] = float(np.clip(best["master"] + 20 * np.log10(0.708 / peak), -36, 6))
    a = render_batch([best], target.midi, hold, tail)[0]
    return MatchResult(best, best_d, start_d, best_name, scored[0][0], renders + 2, target, analyze_array(a), a)


# ----------------------------------------------------------------------------- preset files
def save_preset(path: str, name: str, params: dict, notes: str = "") -> str:
    doc = {"format": "DOA1 preset", "version": 1, "name": name, "notes": notes,
           "params": {k: float(v) for k, v in params.items() if k in mp.DEFAULTS}}
    with open(path, "w") as f: json.dump(doc, f, indent=1)
    return path


def load_preset(path: str) -> tuple[str, dict]:
    doc = json.load(open(path))
    return doc.get("name", os.path.basename(path)), {**mp.DEFAULTS, **doc.get("params", {})}
