#!/usr/bin/env python3
"""Generates Source/FactoryPresets.inc from compact preset definitions.

Run:  python3 tools/make_presets.py [levels.json]
levels.json (optional) maps preset name -> master dB, produced by the
level-matching pass of the smoke test, so all presets play at a similar loudness.
"""
import json, sys, os

# ---- enum indices (must match the plugin) ----
SAW, PULSE, TRI, SINE, NOISE = range(5)
LP, BP, HP = range(3)
T = dict(none=0, cutA=1, cutB=2, cut=3, pitch=4, pitchA=5, pitchB=6, reso=7, pan=8, vol=9, detune=10, drive=11, balance=12)
S = dict(none=0, vel=1, wheel=2, at=3, key=4, lfo1=5, lfo2=6, env1=7, env2=8, rand=9, vib=10)
LW = dict(sine=0, tri=1, saw=2, square=3, sh=4)
DT = {"1/16": 0, "1/8T": 1, "1/8": 2, "1/8D": 3, "1/4T": 4, "1/4": 5, "1/4D": 6, "1/2": 7}
AR = {"1/4": 0, "1/8": 1, "1/8T": 2, "1/16": 3, "1/16T": 4, "1/32": 5}
AM = dict(up=0, down=1, updown=2, played=3, random=4, chord=5)
OSCS = ["a1", "a2", "b1", "b2"]

# ---- building blocks: each returns a dict of parameter id -> real value ----
def osc(slot, wave=SAW, oct=0, semi=0, fine=0, vol=0.7, pan=0, voices=1, det=0.2, retrig=False):
    p = slot
    return {f"{p}_on": 1, f"{p}_wave": wave, f"{p}_oct": oct, f"{p}_semi": semi, f"{p}_fine": fine,
            f"{p}_vol": vol, f"{p}_pan": pan, f"{p}_voices": voices, f"{p}_detune": det, f"{p}_retrig": int(retrig)}

def filt(part, type=LP, slope=24, cut=20000, reso=0.0, drive=0.0, key=0.0):
    return {f"{part}_type": type, f"{part}_slope": 1 if slope == 24 else 0, f"{part}_cutoff": cut,
            f"{part}_reso": reso, f"{part}_drive": drive, f"{part}_key": key}

def amp(part, a=0.002, d=0.3, s=1.0, r=0.2):
    return {f"{part}_a": a, f"{part}_d": d, f"{part}_s": s, f"{part}_r": r}

def amps(a=0.002, d=0.3, s=1.0, r=0.2):
    return {**amp("a", a, d, s, r), **amp("b", a, d, s, r)}

def menv(n, a=0.001, d=0.3, s=0.0, r=0.3, amt=0.0, to="none"):
    return {f"m{n}_a": a, f"m{n}_d": d, f"m{n}_s": s, f"m{n}_r": r, f"m{n}_amt": amt, f"m{n}_target": T[to]}

def lfo(n, wave="sine", rate=1.0, amt=0.0, to="none"):
    return {f"l{n}_wave": LW[wave], f"l{n}_rate": rate, f"l{n}_amt": amt, f"l{n}_target": T[to]}

def mm(n, src, dst, amt):
    return {f"mm{n}_src": S[src], f"mm{n}_dst": T[dst], f"mm{n}_amt": amt}

def glob(mode=0, poly=8, porta=0.0, bend=2):
    return {"mode": mode, "poly": poly, "porta": porta, "bend": bend}

def dist(type=0, amt=0.3, mix=1.0):          return {"dist_on": 1, "dist_type": type, "dist_amt": amt, "dist_mix": mix}
def phaser(rate=0.3, depth=0.6, fb=0.4, mix=0.5): return {"ph_on": 1, "ph_rate": rate, "ph_depth": depth, "ph_fb": fb, "ph_mix": mix}
def chorus(rate=0.6, depth=0.4, delay=12, mix=0.4): return {"ch_on": 1, "ch_rate": rate, "ch_depth": depth, "ch_delay": delay, "ch_mix": mix}
def eq(low=0.0, high=0.0):                   return {"eq_on": 1, "eq_low": low, "eq_high": high}
def delay(time="1/8D", fb=0.35, mix=0.25, tone=6000, pp=True):
    return {"dl_on": 1, "dl_time": DT[time], "dl_fb": fb, "dl_mix": mix, "dl_tone": tone, "dl_pp": int(pp)}
def reverb(size=0.7, mix=0.25, damp=0.5, width=1.0): return {"rv_on": 1, "rv_size": size, "rv_mix": mix, "rv_damp": damp, "rv_width": width}
def comp(thresh=-12, ratio=4, att=5, rel=120, makeup=0): return {"cp_on": 1, "cp_thresh": thresh, "cp_ratio": ratio, "cp_att": att, "cp_rel": rel, "cp_makeup": makeup}
def arp(mode="up", rate="1/16", octs=1, gate=0.5, swing=0.0, hold=False):
    return {"arp_on": 1, "arp_mode": AM[mode], "arp_rate": AR[rate], "arp_oct": octs, "arp_gate": gate, "arp_swing": swing, "arp_hold": int(hold)}

OFF_A1 = {"a1_on": 0}   # Init has A1 on; everything else is off by default

def P(name, *parts):
    d = {}
    for p in parts: d.update(p)
    return (name, d)

# =============================================================================
PRESETS = [
    P("Init"),

    # ---------------------------------------------------------------- LEADS
    P("Lead - Supersaw",
      osc("a1", voices=8, det=0.35, vol=0.8), osc("a2", oct=1, voices=6, det=0.3, vol=0.5), osc("b1", oct=-1, voices=4, det=0.25, vol=0.45),
      filt("a", cut=9000, reso=0.1), filt("b", cut=6000), amps(0.003, 0.5, 0.85, 0.35),
      mm(1, "wheel", "detune", 0.8), delay("1/4D", 0.3, 0.18, 5000), reverb(0.8, 0.22)),
    P("Lead - Uplifting Anthem",
      osc("a1", voices=8, det=0.42, vol=0.75), osc("a2", voices=8, det=0.3, semi=12, vol=0.45), osc("b1", PULSE, oct=-1, voices=4, det=0.2, vol=0.35),
      osc("b2", SAW, oct=1, semi=7, voices=2, det=0.15, vol=0.18),
      filt("a", cut=11000, reso=0.05), filt("b", cut=7000), amps(0.004, 0.6, 0.9, 0.45),
      lfo(1, "sine", 5.5, 0.0), mm(1, "vib", "pitch", 0.012),
      chorus(0.35, 0.3, 14, 0.25), delay("1/4D", 0.35, 0.2, 5500), reverb(0.88, 0.28), comp(-14, 3, 10, 150, 2)),
    P("Lead - Hard Dist",
      osc("a1", voices=4, det=0.2), osc("a2", PULSE, voices=2, det=0.1, vol=0.5),
      filt("a", cut=5000, reso=0.25, drive=0.4), amps(0.002, 0.3, 0.9, 0.2),
      dist(1, 0.5, 0.8), eq(2, 3), delay("1/8D", 0.25, 0.15), reverb(0.5, 0.12), comp(-16, 4, 3, 100, 3)),
    P("Lead - Mono Glide",
      glob(mode=2, porta=0.12), osc("a1", voices=2, det=0.08), osc("a2", oct=-1, vol=0.5),
      filt("a", cut=2500, reso=0.35, key=0.6), menv(1, 0.001, 0.4, 0.3, 0.3, 0.3, "cutA"), amps(0.002, 0.2, 1.0, 0.15),
      lfo(1, "sine", 5.0, 0.0), mm(1, "vib", "pitch", 0.015), mm(2, "vel", "cutA", 0.25),
      delay("1/8D", 0.35, 0.2), reverb(0.6, 0.15)),
    P("Lead - Square Retro",
      osc("a1", PULSE, vol=0.6), osc("a2", PULSE, oct=1, fine=7, vol=0.3),
      filt("a", cut=7000, slope=12), amps(0.002, 0.2, 0.8, 0.1),
      lfo(1, "tri", 6.0, 0.006, "pitch"), delay("1/8", 0.3, 0.2, 4000, pp=False), reverb(0.4, 0.1)),
    P("Lead - Fifth Saw",
      osc("a1", voices=3, det=0.18), osc("a2", semi=7, voices=3, det=0.18, vol=0.55),
      filt("a", cut=4500, reso=0.2, key=0.4), menv(1, 0.001, 0.5, 0.4, 0.3, 0.25, "cutA"), amps(0.002, 0.4, 0.85, 0.25),
      chorus(0.5, 0.35, 10, 0.3), delay("1/4", 0.3, 0.18), reverb(0.65, 0.18)),
    P("Lead - Soft Sine",
      glob(mode=2, porta=0.06), osc("a1", SINE, vol=0.8), osc("a2", TRI, oct=1, vol=0.25),
      amps(0.01, 0.4, 0.9, 0.35), lfo(1, "sine", 5.2, 0.0), mm(1, "vib", "pitch", 0.02), mm(2, "at", "vol", 0.3),
      delay("1/4D", 0.4, 0.28, 3500), reverb(0.85, 0.3)),
    P("Lead - Acid Squelch",
      glob(mode=2, porta=0.06), osc("a1", vol=0.8),
      filt("a", cut=500, reso=0.82, drive=0.5), menv(1, 0.001, 0.25, 0.0, 0.2, 0.55, "cutA"), amps(0.002, 0.3, 1.0, 0.08),
      mm(1, "vel", "cutA", 0.3), mm(2, "wheel", "reso", 0.2), dist(0, 0.35, 0.7), delay("1/8D", 0.3, 0.15, 3000)),
    P("Lead - Psy Screamer",
      glob(mode=1), osc("a1", voices=2, det=0.1), osc("a2", PULSE, oct=1, vol=0.4),
      filt("a", BP, 12, cut=1800, reso=0.6, drive=0.3), lfo(1, "saw", 7.0, 0.35, "cutA"), amps(0.002, 0.2, 1.0, 0.1),
      dist(1, 0.4, 0.6), phaser(0.8, 0.7, 0.6, 0.4), delay("1/8D", 0.45, 0.25, 4000)),
    P("Lead - Chiptune",
      osc("a1", PULSE, vol=0.55), osc("a2", PULSE, oct=1, vol=0.2),
      filt("a", cut=12000, slope=12), amps(0.001, 0.1, 0.7, 0.05),
      lfo(1, "sine", 6.0, 0.0), mm(1, "vib", "pitch", 0.02), dist(3, 0.45, 0.6), delay("1/16", 0.25, 0.12, 6000, pp=False)),
    P("Lead - Detuned Pulse",
      osc("a1", PULSE, voices=6, det=0.3), osc("a2", SAW, oct=-1, voices=2, det=0.1, vol=0.4),
      filt("a", cut=6000, reso=0.15), amps(0.003, 0.4, 0.85, 0.3), phaser(0.25, 0.5, 0.3, 0.3), delay("1/4D", 0.3, 0.2), reverb(0.7, 0.2)),

    # ---------------------------------------------------------------- PLUCKS
    P("Pluck - Trance",
      osc("a1", voices=6, det=0.25), osc("a2", PULSE, oct=1, voices=2, det=0.1, vol=0.4),
      filt("a", cut=300, reso=0.25, key=0.5), menv(1, 0.001, 0.22, 0, 0.2, 0.55, "cutA"), amp("a", 0.002, 0.45, 0, 0.3),
      delay("1/8D", 0.4, 0.28, 4000), reverb(0.75, 0.2)),
    P("Pluck - Bell",
      OFF_A1, osc("b1", SINE, vol=0.7), osc("b2", TRI, oct=2, semi=7, vol=0.25),
      amp("b", 0.001, 0.9, 0, 0.8), menv(1, 0.001, 0.08, 0, 0.1, 0.25, "pitchB"),
      delay("1/8D", 0.35, 0.22, 7000), reverb(0.8, 0.28)),
    P("Pluck - Dark Melodic",
      osc("a1", voices=3, det=0.12), osc("a2", TRI, oct=-1, vol=0.5),
      filt("a", cut=180, reso=0.35, key=0.6), menv(1, 0.001, 0.3, 0, 0.3, 0.42, "cutA"), amp("a", 0.002, 0.6, 0, 0.4),
      mm(1, "vel", "cutA", 0.2), delay("1/8D", 0.45, 0.3, 2500), reverb(0.85, 0.3, 0.7)),
    P("Pluck - Short 16th",
      osc("a1", voices=4, det=0.2), filt("a", cut=400, reso=0.3, key=0.5),
      menv(1, 0.001, 0.12, 0, 0.1, 0.5, "cutA"), amp("a", 0.001, 0.18, 0, 0.1), delay("1/16", 0.3, 0.18, 5000), reverb(0.5, 0.12)),
    P("Pluck - Pizzicato",
      osc("a1", voices=2, det=0.08), osc("a2", TRI, vol=0.5),
      filt("a", cut=900, reso=0.1, key=0.8), menv(1, 0.001, 0.15, 0, 0.1, 0.35, "cutA"), amp("a", 0.001, 0.25, 0, 0.15),
      reverb(0.7, 0.25)),
    P("Pluck - Koto",
      osc("a1", PULSE, vol=0.6), osc("a2", SAW, oct=1, vol=0.3),
      filt("a", BP, 12, cut=1200, reso=0.45, key=0.7), menv(1, 0.001, 0.2, 0, 0.2, 0.3, "cutA"), amp("a", 0.001, 0.7, 0, 0.5),
      delay("1/4", 0.3, 0.2, 5000), reverb(0.7, 0.22)),
    P("Pluck - House Chord",
      osc("a1", voices=4, det=0.18), osc("a2", PULSE, voices=2, det=0.1, vol=0.45),
      filt("a", cut=700, reso=0.2, key=0.5), menv(1, 0.001, 0.25, 0.05, 0.2, 0.45, "cutA"), amp("a", 0.002, 0.35, 0.05, 0.2),
      chorus(0.5, 0.3, 12, 0.25), delay("1/8", 0.25, 0.15), reverb(0.6, 0.15)),
    P("Pluck - Glass",
      osc("a1", TRI, vol=0.7), osc("a2", SINE, oct=2, vol=0.3),
      filt("a", HP, 12, cut=300), amp("a", 0.001, 0.5, 0, 0.6), menv(1, 0.001, 0.04, 0, 0.05, 0.2, "pitchA"),
      chorus(0.3, 0.3, 15, 0.3), delay("1/8D", 0.45, 0.3, 8000), reverb(0.9, 0.35, 0.3)),
    P("Pluck - Marimba",
      osc("a1", SINE, vol=0.8), osc("a2", SINE, oct=2, semi=4, vol=0.15),
      amp("a", 0.001, 0.35, 0, 0.3), menv(1, 0.001, 0.03, 0, 0.03, 0.3, "pitchA"), reverb(0.55, 0.15)),

    # ---------------------------------------------------------------- PADS
    P("Pad - Warm",
      osc("a1", voices=8, det=0.25), osc("a2", PULSE, oct=-1, voices=4, det=0.2, vol=0.5), osc("b1", TRI, vol=0.5),
      filt("a", cut=1800, reso=0.2), filt("b", slope=12, cut=2500), amps(1.2, 1.0, 0.9, 2.5),
      lfo(1, "sine", 0.15, 0.12, "cut"), chorus(0.4, 0.5, 12, 0.45), reverb(0.92, 0.35, 0.6)),
    P("Pad - Dark",
      osc("a1", voices=6, det=0.2), osc("a2", oct=-1, voices=4, det=0.15, vol=0.6),
      filt("a", cut=600, reso=0.3), amp("a", 1.5, 1.0, 1.0, 3.0), lfo(1, "tri", 0.08, 0.15, "cutA"),
      reverb(0.95, 0.4, 0.8)),
    P("Pad - Strings",
      osc("a1", voices=7, det=0.22), osc("a2", oct=1, voices=5, det=0.2, vol=0.35),
      filt("a", cut=3500, reso=0.05, key=0.3), amp("a", 0.4, 1.0, 0.9, 0.8),
      lfo(1, "sine", 4.5, 0.0), mm(1, "vib", "pitch", 0.01), mm(2, "at", "cutA", 0.2), chorus(0.6, 0.4, 14, 0.35), reverb(0.85, 0.3)),
    P("Pad - Choir",
      osc("a1", voices=5, det=0.15), osc("a2", PULSE, voices=3, det=0.1, vol=0.5),
      filt("a", BP, 12, cut=900, reso=0.45), amp("a", 0.8, 1.0, 0.9, 1.5), lfo(1, "sine", 0.3, 0.12, "cutA"),
      chorus(0.4, 0.6, 18, 0.5), reverb(0.9, 0.38)),
    P("Pad - Evolving S&H",
      osc("a1", voices=6, det=0.3), osc("b1", PULSE, oct=1, voices=3, det=0.2, vol=0.4),
      filt("a", cut=1500, reso=0.4), filt("b", BP, 12, cut=2000, reso=0.5), amps(1.0, 1.0, 0.9, 2.0),
      lfo(1, "sh", 4.0, 0.25, "cutB"), lfo(2, "sine", 0.1, 0.2, "cutA"), delay("1/8D", 0.4, 0.2), reverb(0.9, 0.35)),
    P("Pad - Ambient Shimmer",
      osc("a1", TRI, voices=6, det=0.2), osc("a2", SINE, oct=1, voices=4, det=0.15, vol=0.5), osc("b1", SAW, oct=2, voices=4, det=0.25, vol=0.15),
      filt("b", HP, 12, cut=2000), amps(2.0, 1.0, 1.0, 4.0), chorus(0.3, 0.5, 16, 0.4), delay("1/4D", 0.55, 0.3, 7000), reverb(0.98, 0.55, 0.2)),
    P("Pad - Sweep",
      osc("a1", voices=8, det=0.3), osc("a2", oct=-1, voices=4, det=0.2, vol=0.5),
      filt("a", cut=250, reso=0.45), menv(1, 2.5, 3.0, 0.6, 2.0, 0.6, "cutA"), amp("a", 0.8, 1.0, 1.0, 2.5),
      phaser(0.1, 0.6, 0.4, 0.3), reverb(0.9, 0.35)),
    P("Pad - Pulse Phase",
      osc("a1", PULSE, voices=4, det=0.25), osc("a2", PULSE, fine=-12, voices=4, det=0.2, vol=0.6),
      filt("a", cut=2200, reso=0.15), amp("a", 0.6, 1.0, 0.85, 1.8), phaser(0.12, 0.8, 0.6, 0.5), reverb(0.85, 0.3)),
    P("Pad - Lush Poly",
      osc("a1", voices=6, det=0.28), osc("a2", voices=6, det=0.22, semi=12, vol=0.35), osc("b1", PULSE, voices=4, det=0.2, vol=0.4),
      filt("a", cut=2800, reso=0.15), filt("b", cut=1600, reso=0.2), amps(0.3, 1.2, 0.8, 1.2),
      menv(1, 0.3, 1.5, 0.4, 1.0, 0.2, "cut"), chorus(0.5, 0.45, 12, 0.4), reverb(0.85, 0.3)),

    # ---------------------------------------------------------------- BASS
    P("Bass - Reese",
      glob(mode=1, porta=0.05), osc("a1", oct=-2, voices=2, det=0.12), osc("a2", oct=-2, fine=8, voices=2, det=0.1),
      osc("b1", SINE, oct=-3, vol=0.6), filt("a", cut=700, reso=0.15, drive=0.4), amps(0.002, 0.3, 1.0, 0.15),
      lfo(1, "tri", 0.25, 0.08, "cutA"), dist(0, 0.35, 0.6), comp(-18, 4, 5, 120, 4)),
    P("Bass - Sub",
      glob(mode=2, porta=0.03), OFF_A1, osc("b1", SINE, oct=-2, vol=0.9), osc("b2", TRI, oct=-2, vol=0.15),
      amp("b", 0.003, 0.3, 1.0, 0.12), comp(-14, 3, 10, 100, 2)),
    P("Bass - Rolling Psy",
      glob(mode=1), osc("a1", oct=-2, vol=0.8, retrig=True),
      filt("a", cut=200, reso=0.3, drive=0.3), menv(1, 0.001, 0.09, 0, 0.05, 0.45, "cutA"), amp("a", 0.001, 0.11, 0, 0.03),
      comp(-16, 4, 2, 60, 3)),
    P("Bass - Trance Offbeat",
      glob(mode=1), osc("a1", oct=-2, voices=3, det=0.1), osc("a2", PULSE, oct=-1, vol=0.3),
      filt("a", cut=350, reso=0.2, key=0.3), menv(1, 0.001, 0.15, 0.1, 0.1, 0.4, "cutA"), amp("a", 0.001, 0.2, 0.3, 0.06),
      comp(-14, 3, 5, 80, 2)),
    P("Bass - Acid 303",
      glob(mode=2, porta=0.06), osc("a1", oct=-1, vol=0.8),
      filt("a", cut=300, reso=0.8, drive=0.45, key=0.4), menv(1, 0.001, 0.22, 0, 0.15, 0.55, "cutA"), amp("a", 0.002, 0.3, 1.0, 0.08),
      mm(1, "vel", "cutA", 0.3), mm(2, "wheel", "cutA", 0.3), dist(0, 0.3, 0.6)),
    P("Bass - Melodic Techno",
      glob(mode=1, porta=0.02), osc("a1", oct=-2, voices=2, det=0.06), osc("b1", SINE, oct=-2, vol=0.5),
      filt("a", cut=400, reso=0.25, drive=0.5), menv(1, 0.001, 0.4, 0.2, 0.3, 0.3, "cutA"), amps(0.002, 0.4, 0.8, 0.2),
      lfo(1, "sine", 0.12, 0.08, "cutA"), dist(0, 0.25, 0.5), comp(-16, 4, 8, 120, 3)),
    P("Bass - Wobble",
      glob(mode=1), osc("a1", oct=-2, voices=3, det=0.12), osc("b1", SINE, oct=-2, vol=0.5),
      filt("a", cut=300, reso=0.5, drive=0.4), lfo(1, "sine", 3.2, 0.35, "cutA"), amps(0.002, 0.3, 1.0, 0.15),
      mm(1, "wheel", "drive", 0.5), dist(0, 0.4, 0.6), comp(-18, 4, 5, 100, 4)),
    P("Bass - Pluck",
      glob(mode=1), osc("a1", oct=-2, voices=2, det=0.08), osc("a2", PULSE, oct=-1, vol=0.35),
      filt("a", cut=250, reso=0.25), menv(1, 0.001, 0.18, 0, 0.1, 0.45, "cutA"), amp("a", 0.001, 0.35, 0, 0.1)),
    P("Bass - Growl",
      glob(mode=1, porta=0.04), osc("a1", oct=-2, voices=2, det=0.1), osc("a2", PULSE, oct=-1, voices=2, det=0.08, vol=0.5),
      filt("a", BP, 12, cut=600, reso=0.5, drive=0.5), lfo(1, "tri", 1.8, 0.3, "cutA"), amps(0.002, 0.3, 1.0, 0.15),
      dist(2, 0.45, 0.7), comp(-18, 5, 3, 90, 5)),
    P("Bass - Deep House",
      glob(mode=2, porta=0.04), osc("a1", PULSE, oct=-2, vol=0.6), osc("b1", SINE, oct=-2, vol=0.6),
      filt("a", cut=500, reso=0.2), menv(1, 0.001, 0.25, 0.3, 0.2, 0.25, "cutA"), amps(0.002, 0.3, 0.9, 0.12), comp(-14, 3, 10, 120, 2)),

    # ---------------------------------------------------------------- KEYS
    P("Keys - Electric Piano",
      osc("a1", SINE, vol=0.7), osc("a2", TRI, oct=1, vol=0.25),
      amp("a", 0.002, 1.5, 0.15, 0.5), menv(1, 0.001, 0.05, 0, 0.05, 0.08, "pitchA"),
      mm(1, "vel", "vol", 0.3), lfo(1, "sine", 4.0, 0.25, "pan"), chorus(0.5, 0.3, 10, 0.25), reverb(0.6, 0.18)),
    P("Keys - Organ",
      osc("a1", SINE, vol=0.6), osc("a2", SINE, oct=1, vol=0.4), osc("b1", SINE, oct=2, vol=0.25), osc("b2", PULSE, oct=1, vol=0.08),
      amps(0.005, 0.1, 1.0, 0.08), chorus(5.0, 0.15, 6, 0.4), reverb(0.6, 0.15)),
    P("Keys - House Piano",
      osc("a1", voices=2, det=0.08), osc("a2", PULSE, oct=1, vol=0.35), osc("b1", SINE, vol=0.5),
      filt("a", cut=2500, reso=0.1, key=0.5), menv(1, 0.001, 0.6, 0.1, 0.3, 0.25, "cutA"), amps(0.001, 1.2, 0.0, 0.4),
      mm(1, "vel", "cutA", 0.3), reverb(0.55, 0.15)),
    P("Keys - Clav",
      osc("a1", PULSE, vol=0.7), filt("a", BP, 12, cut=1500, reso=0.4, key=0.8),
      menv(1, 0.001, 0.15, 0, 0.1, 0.3, "cutA"), amp("a", 0.001, 0.5, 0.1, 0.1), mm(1, "vel", "cutA", 0.3), phaser(0.5, 0.5, 0.3, 0.3)),
    P("Keys - Bells",
      OFF_A1, osc("b1", SINE, vol=0.6), osc("b2", SINE, oct=1, semi=7, fine=3, vol=0.35),
      amp("b", 0.001, 2.0, 0, 1.5), delay("1/4", 0.35, 0.2, 7000), reverb(0.85, 0.3)),

    # ---------------------------------------------------------------- STABS
    P("Stab - Melodic",
      osc("a1", voices=4, det=0.2), osc("a2", semi=7, voices=4, det=0.2, vol=0.6), osc("b1", PULSE, oct=1, vol=0.3),
      filt("a", cut=900, reso=0.35), filt("b", cut=1500), menv(1, 0.001, 0.35, 0.1, 0.3, 0.45, "cut"), amps(0.002, 0.6, 0.2, 0.4),
      delay("1/8", 0.35, 0.22), phaser(0.15, 0.6, 0.4, 0.35), reverb(0.7, 0.2)),
    P("Stab - Rave Hoover",
      osc("a1", voices=8, det=0.5), osc("a2", PULSE, oct=-1, voices=6, det=0.4, vol=0.6),
      filt("a", cut=3000, reso=0.2), menv(1, 0.001, 0.15, 0, 0.1, -0.12, "pitch"), amps(0.002, 0.4, 0.6, 0.3),
      chorus(0.8, 0.6, 10, 0.4), dist(0, 0.25, 0.5), reverb(0.6, 0.15)),
    P("Stab - Dub Chord",
      osc("a1", voices=2, det=0.1), osc("a2", PULSE, vol=0.5),
      filt("a", cut=700, reso=0.3), menv(1, 0.001, 0.2, 0, 0.2, 0.35, "cutA"), amp("a", 0.001, 0.3, 0, 0.2),
      delay("1/4D", 0.6, 0.4, 2500), reverb(0.85, 0.3, 0.7)),
    P("Stab - Brass",
      osc("a1", voices=4, det=0.15), osc("a2", voices=3, det=0.12, semi=12, vol=0.3),
      filt("a", cut=600, reso=0.15, key=0.5), menv(1, 0.03, 0.4, 0.5, 0.3, 0.4, "cutA"), amp("a", 0.03, 0.5, 0.8, 0.25),
      mm(1, "vel", "cutA", 0.25), reverb(0.6, 0.15)),
    P("Stab - Detroit",
      osc("a1", voices=3, det=0.15), osc("a2", semi=3, voices=3, det=0.15, vol=0.6), osc("b1", semi=7, voices=3, det=0.15, vol=0.6),
      filt("a", cut=1200, reso=0.3), filt("b", cut=1200, reso=0.3), menv(1, 0.001, 0.25, 0, 0.2, 0.35, "cut"), amps(0.001, 0.4, 0, 0.3),
      chorus(0.4, 0.4, 12, 0.3), delay("1/8D", 0.35, 0.2), reverb(0.6, 0.2)),

    # ---------------------------------------------------------------- ARPS & SEQUENCES
    P("Arp - Melodic Sequence",
      osc("a1", voices=3, det=0.15), osc("a2", PULSE, oct=1, vol=0.35),
      filt("a", cut=600, reso=0.3), menv(1, 0.001, 0.18, 0, 0.3, 0.5, "cutA"), amp("a", 0.002, 0.3, 0, 0.2),
      arp("updown", "1/16", 2, 0.45), lfo(2, "sine", 0.07, 0.0),
      mm(1, "vel", "cutA", 0.3), mm(2, "wheel", "reso", 0.4), mm(3, "lfo2", "cutA", 0.15),
      delay("1/8D", 0.35, 0.25), reverb(0.7, 0.2)),
    P("Arp - Trance Gate",
      osc("a1", voices=7, det=0.3), osc("a2", oct=1, voices=4, det=0.25, vol=0.4),
      filt("a", cut=3500, reso=0.15), amp("a", 0.001, 0.2, 0.7, 0.05),
      arp("chord", "1/16", 1, 0.55), delay("1/8D", 0.3, 0.18), reverb(0.8, 0.25)),
    P("Arp - Random Bubbles",
      osc("a1", SINE, vol=0.7), osc("a2", TRI, oct=1, vol=0.3),
      filt("a", cut=3000), amp("a", 0.001, 0.15, 0, 0.1), menv(1, 0.001, 0.04, 0, 0.04, 0.15, "pitchA"),
      arp("random", "1/16", 3, 0.4), delay("1/8D", 0.5, 0.3, 6000), reverb(0.85, 0.3)),
    P("Arp - Pulse Chords",
      osc("a1", PULSE, voices=3, det=0.15), osc("a2", SAW, oct=-1, voices=2, det=0.1, vol=0.45),
      filt("a", cut=900, reso=0.35), menv(1, 0.001, 0.12, 0, 0.1, 0.4, "cutA"), amp("a", 0.001, 0.2, 0, 0.1),
      arp("chord", "1/8", 1, 0.6, swing=0.15), lfo(1, "sine", 0.1, 0.2, "cutA"), delay("1/8D", 0.35, 0.2), reverb(0.6, 0.15)),
    P("Arp - Octave Bass",
      glob(poly=4), osc("a1", oct=-2, voices=2, det=0.08), osc("b1", SINE, oct=-2, vol=0.5),
      filt("a", cut=350, reso=0.3), menv(1, 0.001, 0.12, 0, 0.1, 0.4, "cutA"), amps(0.001, 0.15, 0, 0.05),
      arp("up", "1/16", 2, 0.5), comp(-14, 3, 5, 80, 2)),
    P("Arp - Psy Triplet",
      osc("a1", voices=2, det=0.1), osc("a2", PULSE, oct=1, vol=0.4),
      filt("a", BP, 12, cut=1400, reso=0.5), lfo(1, "saw", 0.25, 0.3, "cutA"), amp("a", 0.001, 0.1, 0, 0.05),
      arp("updown", "1/16T", 2, 0.5), phaser(0.4, 0.6, 0.5, 0.35), delay("1/8D", 0.45, 0.25)),
    P("Arp - Melodic Techno Bounce",
      osc("a1", voices=4, det=0.2), osc("b1", TRI, oct=1, vol=0.35),
      filt("a", cut=500, reso=0.4), filt("b", HP, 12, cut=800), menv(1, 0.001, 0.2, 0, 0.2, 0.45, "cutA"), amps(0.001, 0.25, 0, 0.2),
      arp("played", "1/16", 1, 0.4, swing=0.1), lfo(2, "sine", 0.05, 0.0), mm(1, "lfo2", "cutA", 0.25), mm(2, "rand", "pan", 0.4),
      delay("1/8D", 0.45, 0.3, 3500), reverb(0.85, 0.3)),

    # ---------------------------------------------------------------- FX
    P("FX - Noise Riser",
      OFF_A1, osc("b1", NOISE, vol=0.7), osc("b2", SAW, voices=4, det=0.4, vol=0.25),
      filt("b", BP, 12, cut=300, reso=0.5), menv(1, 4.0, 1.0, 1.0, 1.0, 0.75, "cutB"), menv(2, 4.0, 1.0, 1.0, 1.0, 0.3, "pitchB"),
      amp("b", 3.0, 1.0, 1.0, 1.5), phaser(0.5, 0.7, 0.5, 0.4), reverb(0.9, 0.4)),
    P("FX - Downlifter",
      osc("a1", voices=6, det=0.4), osc("b1", NOISE, vol=0.4),
      filt("a", cut=8000), filt("b", HP, 12, cut=1000), menv(1, 0.001, 3.0, 0, 1.0, -0.8, "pitch"), menv(2, 0.001, 3.0, 0, 1.0, -0.6, "cut"),
      amps(0.001, 3.0, 0, 1.0), reverb(0.9, 0.4)),
    P("FX - Impact",
      osc("a1", SINE, oct=-2, vol=0.9), osc("b1", NOISE, vol=0.6),
      filt("b", LP, 12, cut=3000), menv(1, 0.001, 0.4, 0, 0.3, -0.25, "pitchA"), amp("a", 0.001, 1.5, 0, 1.0), amp("b", 0.001, 0.4, 0, 0.3),
      dist(0, 0.3, 0.5), reverb(0.95, 0.45, 0.6)),
    P("FX - Wind",
      OFF_A1, osc("b1", NOISE, vol=0.8), filt("b", BP, 12, cut=800, reso=0.6),
      lfo(1, "sine", 0.15, 0.3, "cutB"), lfo(2, "sh", 0.8, 0.15, "cutB"), amp("b", 1.5, 1.0, 1.0, 2.0), reverb(0.95, 0.4)),
    P("FX - Laser Zap",
      osc("a1", PULSE, oct=1, vol=0.6), menv(1, 0.001, 0.15, 0, 0.1, -0.9, "pitchA"), amp("a", 0.001, 0.25, 0, 0.1),
      delay("1/8D", 0.5, 0.3, 5000)),
    P("FX - Alarm",
      osc("a1", PULSE, oct=1, vol=0.5), lfo(1, "square", 4.0, 0.2, "pitch"), filt("a", cut=3000, slope=12),
      amp("a", 0.01, 0.3, 1.0, 0.2), reverb(0.6, 0.2)),
]

def cpp_string(name, values, levels):
    vals = dict(values)
    if name in levels: vals["master"] = levels[name]
    elif "master" not in vals: vals["master"] = -9.0
    def lit(v):
        t = f"{float(v):.6g}"
        if "." not in t and "e" not in t: t += ".0"
        return t + "f"
    items = ", ".join(f'{{ "{k}", {lit(v)} }}' for k, v in vals.items())
    return f'    {{ "{name}", {{ {items} }} }},'

def main():
    levels = {}
    if len(sys.argv) > 1 and os.path.exists(sys.argv[1]):
        levels = json.load(open(sys.argv[1]))
    names = [n for n, _ in PRESETS]
    assert len(names) == len(set(names)), "duplicate preset names"
    out = ["// Generated by tools/make_presets.py - do not edit by hand.", ""]
    out += [cpp_string(n, v, levels) for n, v in PRESETS]
    path = os.path.join(os.path.dirname(__file__), "..", "Source", "FactoryPresets.inc")
    open(path, "w").write("\n".join(out) + "\n")
    print(f"wrote {len(PRESETS)} presets to {os.path.normpath(path)}")

if __name__ == "__main__":
    main()
