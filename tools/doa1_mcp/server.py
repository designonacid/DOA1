#!/usr/bin/env python3
"""DOA1 MCP server: lets an LLM turn an audio file into a DOA1 preset and refine it.

Tools
  analyze_audio        describe a sound (pitch, envelope, brightness movement, waveform, width...)
  match_audio          search DOA1 for a preset that sounds close to an audio file; saves a .doa1 file
  render_preset        render a .doa1 preset to a WAV preview and describe it
  tweak_preset         change parameters of a .doa1 preset (e.g. "brighter" -> raise a_cutoff)
  compare              how close a .doa1 preset is to an audio file, feature by feature
  describe_parameters  what every DOA1 parameter does and its range
  list_factory_presets the built-in presets (by category)

Run:  python3 server.py            (stdio transport, for Claude Desktop / Claude Code)
"""
from __future__ import annotations

import os, sys, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import doa1_sound as ds

try:                                   # MCP Python SDK 2.x
    from mcp.server.mcpserver import MCPServer as Server
except ImportError:                    # MCP Python SDK 1.x
    from mcp.server.fastmcp import FastMCP as Server

INSTRUCTIONS = """DOA1 is a 4-oscillator subtractive polysynth (parts A and B, each with two oscillators,
a filter and an amp envelope; 2 mod envelopes, 2 LFOs, a 4-slot mod matrix, an arpeggiator and an
effects rack). Typical workflow: analyze_audio -> match_audio -> listen to the preview -> tweak_preset
with small, targeted changes (use describe_parameters) -> compare to check it got closer. Matching works
best on an isolated one-shot of a single synth sound; say so if the user gives a full mix or a loop.
Preset files (.doa1) open in the DOA1 plugin with its LOAD button."""

mcp = Server("doa1", instructions=INSTRUCTIONS)

OUT_DIR = os.environ.get("DOA1_OUTPUT_DIR", os.path.join(os.path.expanduser("~"), "DOA1 Presets"))

# ---------------------------------------------------------------------------- parameter reference
WAVES = "0 saw, 1 pulse, 2 triangle, 3 sine, 4 noise"
TARGETS = "0 none, 1 cutoff A, 2 cutoff B, 3 cutoff A+B, 4 pitch, 5 pitch A, 6 pitch B, 7 resonance, 8 pan, 9 volume, 10 detune, 11 drive, 12 balance A/B"
SOURCES = "0 none, 1 velocity, 2 mod wheel, 3 aftertouch, 4 key, 5 LFO 1, 6 LFO 2, 7 mod env 1, 8 mod env 2, 9 random, 10 LFO 1 x mod wheel"
PARAMS = [
    (r"(a1|a2|b1|b2)_on", 0, 1, "oscillator on/off (a1+a2 feed filter A, b1+b2 feed filter B)"),
    (r"(a1|a2|b1|b2)_wave", 0, 4, f"waveform: {WAVES}"),
    (r"(a1|a2|b1|b2)_oct", -3, 3, "octave"), (r"(a1|a2|b1|b2)_semi", -12, 12, "semitones"),
    (r"(a1|a2|b1|b2)_fine", -100, 100, "fine tune, cents"), (r"(a1|a2|b1|b2)_vol", 0, 1, "level"),
    (r"(a1|a2|b1|b2)_pan", -1, 1, "pan"), (r"(a1|a2|b1|b2)_voices", 1, 8, "unison voices (more = wider, thicker)"),
    (r"(a1|a2|b1|b2)_detune", 0, 1, "unison detune amount"), (r"(a1|a2|b1|b2)_retrig", 0, 1, "restart phase on each note (punchier)"),
    (r"(a|b)_type", 0, 2, "filter type: 0 lowpass, 1 bandpass, 2 highpass"), (r"(a|b)_slope", 0, 1, "0 = 12 dB, 1 = 24 dB"),
    (r"(a|b)_cutoff", 20, 20000, "filter cutoff Hz (lower = darker)"), (r"(a|b)_reso", 0, 1, "resonance"),
    (r"(a|b)_drive", 0, 1, "filter drive"), (r"(a|b)_key", 0, 1, "cutoff key tracking"),
    (r"(a|b)_a", 0.0005, 10, "amp attack s"), (r"(a|b)_d", 0.0005, 10, "amp decay s"), (r"(a|b)_s", 0, 1, "amp sustain"),
    (r"(a|b)_r", 0.0005, 15, "amp release s"),
    (r"m(1|2)_a", 0.0005, 10, "mod env attack s"), (r"m(1|2)_d", 0.0005, 10, "mod env decay s"), (r"m(1|2)_s", 0, 1, "mod env sustain"),
    (r"m(1|2)_r", 0.0005, 15, "mod env release s"), (r"m(1|2)_amt", -1, 1, "mod env amount (cutoff: x6 octaves, pitch: x24 semitones)"),
    (r"m(1|2)_target", 0, 12, f"mod env target: {TARGETS}"),
    (r"l(1|2)_wave", 0, 4, "LFO wave: 0 sine, 1 triangle, 2 saw, 3 square, 4 sample & hold"), (r"l(1|2)_rate", 0.01, 30, "LFO rate Hz"),
    (r"l(1|2)_amt", -1, 1, "LFO amount"), (r"l(1|2)_target", 0, 12, f"LFO target: {TARGETS}"),
    (r"mm[1-4]_src", 0, 10, f"mod matrix source: {SOURCES}"), (r"mm[1-4]_dst", 0, 12, f"mod matrix destination: {TARGETS}"),
    (r"mm[1-4]_amt", -1, 1, "mod matrix amount"),
    (r"mode", 0, 2, "0 poly, 1 mono, 2 legato"), (r"poly", 1, 16, "polyphony"), (r"porta", 0, 1, "glide s"),
    (r"bend", 0, 24, "pitch bend range"), (r"master", -36, 6, "output dB (after effects)"),
    (r"arp_on", 0, 1, "arpeggiator"), (r"arp_mode", 0, 5, "0 up, 1 down, 2 up/down, 3 as played, 4 random, 5 chord"),
    (r"arp_rate", 0, 5, "0 1/4, 1 1/8, 2 1/8T, 3 1/16, 4 1/16T, 5 1/32"), (r"arp_oct", 1, 4, "octaves"),
    (r"arp_gate", 0.05, 1, "gate"), (r"arp_swing", 0, 0.5, "swing"), (r"arp_hold", 0, 1, "hold"),
    (r"dist_on", 0, 1, "distortion"), (r"dist_type", 0, 3, "0 soft, 1 hard, 2 fold, 3 crush"), (r"dist_amt", 0, 1, "amount"), (r"dist_mix", 0, 1, "mix"),
    (r"ph_on", 0, 1, "phaser"), (r"ph_rate", 0.02, 10, "Hz"), (r"ph_depth", 0, 1, ""), (r"ph_fb", 0, 0.9, "feedback"), (r"ph_mix", 0, 1, ""),
    (r"ch_on", 0, 1, "chorus"), (r"ch_rate", 0.02, 5, "Hz"), (r"ch_depth", 0, 1, ""), (r"ch_delay", 5, 30, "ms"), (r"ch_mix", 0, 1, ""),
    (r"eq_on", 0, 1, "EQ"), (r"eq_low", -15, 15, "bass dB"), (r"eq_high", -15, 15, "treble dB"),
    (r"dl_on", 0, 1, "delay"), (r"dl_time", 0, 7, "0 1/16 .. 3 1/8D .. 5 1/4 .. 7 1/2"), (r"dl_fb", 0, 0.95, "feedback"),
    (r"dl_pp", 0, 1, "ping-pong"), (r"dl_tone", 500, 20000, "Hz"), (r"dl_mix", 0, 1, ""),
    (r"rv_on", 0, 1, "reverb"), (r"rv_size", 0, 1, ""), (r"rv_damp", 0, 1, ""), (r"rv_width", 0, 1, ""), (r"rv_mix", 0, 1, ""),
    (r"cp_on", 0, 1, "compressor"), (r"cp_thresh", -40, 0, "dB"), (r"cp_ratio", 1, 20, ""), (r"cp_att", 0.1, 100, "ms"),
    (r"cp_rel", 10, 1000, "ms"), (r"cp_makeup", 0, 24, "dB"),
]


def _param_info(pid: str):
    for pat, lo, hi, desc in PARAMS:
        if re.fullmatch(pat, pid): return lo, hi, desc
    return None


def _note_to_midi(note) -> int:
    if isinstance(note, (int, float)): return int(note)
    m = re.fullmatch(r"([A-Ga-g])(#|b)?(-?\d)", str(note).strip())
    if not m: raise ValueError(f"note '{note}' not understood (use e.g. C3, F#2 or a MIDI number)")
    base = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[m.group(1).upper()]
    base += {"#": 1, "b": -1}.get(m.group(2) or "", 0)
    return base + 12 * (int(m.group(3)) + 1)


def _out(name: str, ext: str) -> str:
    os.makedirs(OUT_DIR, exist_ok=True)
    safe = re.sub(r"[^A-Za-z0-9 _.-]+", "_", name).strip() or "preset"
    return os.path.join(OUT_DIR, safe + ext)


def _side_by_side(a: dict, b: dict) -> dict:
    keys = ["note", "waveform_guess", "attack_ms", "decay_to_-12dB_ms", "brightness_start_hz", "brightness_after_250ms_hz",
            "filter_movement", "noise_amount", "detune_or_movement", "stereo_width"]
    return {k: {"target": a.get(k), "preset": b.get(k)} for k in keys}


def _key_params(p: dict) -> dict:
    """The parameters that define the character, for the LLM to reason about."""
    keep = [k for k in p if any(k.startswith(x) for x in ("a1_", "a2_", "b1_", "b2_")) and p.get(k[:2] + "_on", 0) > 0.5]
    keep += ["a_cutoff", "a_reso", "a_drive", "a_a", "a_d", "a_s", "a_r", "m1_amt", "m1_target", "m1_d", "m1_s"]
    keep += [k for k in ("dist_on", "ch_on", "rv_on", "dl_on", "ph_on") if p.get(k, 0) > 0.5]
    return {k: round(float(p[k]), 4) for k in keep if k in p}


# ---------------------------------------------------------------------------- tools
@mcp.tool()
def analyze_audio(path: str) -> dict:
    """Analyze an audio file (WAV/AIFF/FLAC; ideally a single synth note). Returns pitch, envelope,
    brightness over time, a waveform guess, noise, unison/detune movement and stereo width."""
    f = ds.analyze_file(os.path.expanduser(path))
    d = ds.describe(f)
    if f.duration > 2.8: d["warning"] = "longer than ~3 s; only the first 3 s are analyzed. A single note works best."
    return d


@mcp.tool()
def match_audio(path: str, name: str = "Matched", effort: str = "normal") -> dict:
    """Search DOA1 for a preset that sounds close to the audio file, starting from the nearest factory
    preset and an analysis-based guess. effort: quick (~10 s), normal (~20 s) or thorough (~60 s).
    Saves '<name>.doa1' (open it with the plugin's LOAD button) and a preview WAV."""
    f = ds.analyze_file(os.path.expanduser(path))
    r = ds.match(f, effort)
    preset = ds.save_preset(_out(name, ".doa1"), name, r.params,
                            notes=f"Matched from {os.path.basename(path)}; started from '{r.closest_factory}'.")
    preview = _out(name + " (preview)", ".wav"); ds.write_wav(preview, r.preview)
    return {"preset_file": preset, "preview_wav": preview, "rendered_note": ds.describe(r.result)["note"],
            "closest_factory_preset": r.closest_factory,
            "distance": {"closest_factory": round(r.factory_distance, 3), "matched": round(r.distance, 3),
                         "scale": "0 = identical; under ~0.8 is close; above ~1.5 is only a rough likeness"},
            "comparison": _side_by_side(ds.describe(r.target), ds.describe(r.result)),
            "key_parameters": _key_params(r.params), "renders": r.renders}


@mcp.tool()
def render_preset(preset_path: str, note: str = "C3", seconds: float = 1.5) -> dict:
    """Render a .doa1 preset playing one held note (e.g. 'A2' or 45) to a WAV preview and describe it."""
    name, p = ds.load_preset(os.path.expanduser(preset_path))
    x = ds.render_batch([ds.prepare(p, 0)], _note_to_midi(note), max(0.1, seconds * 0.6), max(0.3, seconds * 0.4 + 0.3))[0]
    wav = _out(name + f" ({note})", ".wav"); ds.write_wav(wav, x)
    return {"preview_wav": wav, "analysis": ds.describe(ds.analyze_array(x))}


@mcp.tool()
def tweak_preset(preset_path: str, changes: dict, save_as: str = "", target_audio: str = "") -> dict:
    """Change parameters of a .doa1 preset, e.g. {"a_cutoff": 3000, "rv_mix": 0.1}. Values are real
    units (see describe_parameters) and are clamped to valid ranges. Saves to save_as (or overwrites),
    renders a preview, and if target_audio is given reports whether it got closer."""
    path = os.path.expanduser(preset_path)
    name, p = ds.load_preset(path)
    applied, rejected = {}, {}
    for k, v in changes.items():
        info = _param_info(k)
        if info is None: rejected[k] = "unknown parameter"; continue
        lo, hi, _ = info
        applied[k] = float(np.clip(float(v), lo, hi)); p[k] = applied[k]
    before = None
    if target_audio:
        tf = ds.analyze_file(os.path.expanduser(target_audio))
        _, old = ds.load_preset(path)
        hold, tail = max(0.15, min(2.5, tf.duration * 0.6)), max(0.4, tf.duration * 0.4 + 0.3)
        xo, xn = ds.render_batch([ds.prepare(old, tf.cents), ds.prepare(p, tf.cents)], tf.midi, hold, tail)
        before, after = ds.distance(tf, ds.analyze_array(xo)), ds.distance(tf, ds.analyze_array(xn))
    new_name = save_as or name
    out = _out(new_name, ".doa1") if save_as else path
    ds.save_preset(out, new_name, p)
    res = render_preset(out)
    res.update({"preset_file": out, "applied": applied, "rejected": rejected})
    if before is not None:
        res["distance_to_target"] = {"before": round(before, 3), "after": round(after, 3), "closer": after < before}
    return res


@mcp.tool()
def compare(preset_path: str, audio_path: str) -> dict:
    """Compare a .doa1 preset with an audio file, feature by feature, at the audio's pitch."""
    tf = ds.analyze_file(os.path.expanduser(audio_path))
    _, p = ds.load_preset(os.path.expanduser(preset_path))
    hold, tail = max(0.15, min(2.5, tf.duration * 0.6)), max(0.4, tf.duration * 0.4 + 0.3)
    x = ds.render_batch([ds.prepare(p, tf.cents)], tf.midi, hold, tail)[0]
    rf = ds.analyze_array(x)
    return {"distance": round(ds.distance(tf, rf), 3), "comparison": _side_by_side(ds.describe(tf), ds.describe(rf))}


@mcp.tool()
def describe_parameters() -> dict:
    """Every DOA1 preset parameter: id pattern, range and meaning."""
    return {"parameters": [{"id": pat, "min": lo, "max": hi, "meaning": d} for pat, lo, hi, d in PARAMS],
            "tips": {"brighter": "raise a_cutoff (and b_cutoff), or eq_high",
                     "pluckier": "raise m1_amt with m1_target 1 and lower m1_d / m1_s; lower a_s",
                     "wider/thicker": "raise a1_voices and a1_detune, or turn on ch_on",
                     "softer attack": "raise a_a and b_a", "less wash": "lower rv_mix and dl_mix"}}


@mcp.tool()
def list_factory_presets(category: str = "") -> dict:
    """The built-in DOA1 presets, grouped by category (optionally filtered, e.g. 'Bass' or 'Trance')."""
    out = {}
    for n, _ in ds.mp.PRESETS:
        c, _, nm = n.partition(" - ")
        if category and category.lower() not in c.lower(): continue
        out.setdefault(c if nm else "General", []).append(nm or n)
    return out


if __name__ == "__main__":
    mcp.run()
