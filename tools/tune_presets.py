#!/usr/bin/env python3
"""Automatic preset tuner. Renders every preset with tools/preset_lab (the real engine), checks it
against per-category targets, nudges parameters that miss, and repeats. Results go to
tools/tuning.json, which make_presets.py overlays on the hand-written presets.

Targets
  wash   effect tails between notes vs. the notes themselves (dB), per category
  pluck  brightness 90-125 ms into a note / brightness at the start (lower = pluckier)
  level  loudness proxy -16 dB, peak <= -1.5 dBFS (applied last, via master)
"""
import json, subprocess, sys, os, copy
sys.path.insert(0, os.path.dirname(__file__))
import make_presets as mp

LAB = sys.argv[1] if len(sys.argv) > 1 else "./preset_lab"
HERE = os.path.dirname(os.path.abspath(__file__))
WASH_MAX = {"Lead": -12, "Techno Lead": -12, "Pluck": -12, "Keys": -14, "Stab": -11, "Bass": -18, "Arp": -10, "Pad": 2, "FX": 6, "General": 99}
PLUCK_MAX = {"Pluck": 0.55, "Stab": 0.65}
PLUCKY_LEADS = {"Techno Lead - Plucky Saw", "Techno Lead - Analog Mono", "Techno Lead - Grit Stab", "Techno Lead - Afterlife Supersaw",
                "Lead - Acid Squelch", "Techno Lead - Peak Time Acid", "Keys - House Piano", "Keys - Clav"}

def cat(n):
    """Sound type used for targets: last word of the category ("Trance Bass" -> "Bass")."""
    c = n.split(" - ")[0] if " - " in n else "General"
    if c == "Techno Lead": return c
    return c.split()[-1]

def run(presets, levels):
    mp.write_tsv("/tmp/tune.tsv", presets, levels)
    subprocess.run([LAB, "/tmp/tune.tsv", "/tmp/tune.json"], check=True)
    return json.load(open("/tmp/tune.json"))

def main():
    base = [(n, dict(v)) for n, v in mp.PRESETS]
    tuned = {n: {} for n, _ in base}
    cur = lambda: [(n, {**v, **tuned[n]}) for n, v in base]
    getp = lambda n, k: {**mp.DEFAULTS, **dict(base)[n], **tuned[n]}[k]
    for it in range(12):
        m = run(cur(), {})
        fails = 0
        for n, _ in base:
            r = m[n]; c = cat(n)
            if not r["finite"]: print("NON-FINITE", n); fails += 1; continue
            # too washy: pull delay and reverb back
            if r["wash_db"] > WASH_MAX[c]:
                fails += 1
                if getp(n, "dl_on"): tuned[n]["dl_mix"] = round(getp(n, "dl_mix") * 0.72, 3); tuned[n]["dl_fb"] = round(getp(n, "dl_fb") * 0.9, 3)
                if getp(n, "rv_on"): tuned[n]["rv_mix"] = round(getp(n, "rv_mix") * 0.72, 3)
                if not getp(n, "dl_on") and not getp(n, "rv_on"):   # tail comes from release
                    for pt in "ab": tuned[n][f"{pt}_r"] = round(max(0.02, getp(n, f"{pt}_r") * 0.7), 3)
            # not plucky enough: deepen and shorten the filter envelope
            target = PLUCK_MAX.get(c, 0.7 if n in PLUCKY_LEADS else None)
            if target and r["pluck"] > target and getp(n, "m1_target") in (1, 2, 3):
                fails += 1
                tuned[n]["m1_amt"] = round(min(0.85, getp(n, "m1_amt") + 0.07), 3)
                tuned[n]["m1_s"] = round(getp(n, "m1_s") * 0.6, 3)
                tuned[n]["m1_d"] = round(max(0.06, getp(n, "m1_d") * 0.85), 3)
                for pt in "ab":
                    if getp(n, f"{pt}_cutoff") > 250: tuned[n][f"{pt}_cutoff"] = round(getp(n, f"{pt}_cutoff") * 0.85)
        print(f"iteration {it}: {fails} target misses")
        if fails == 0: break
    # level matching through master (post-FX)
    m = run(cur(), {})
    levels = {}
    for it in range(3):
        for n, _ in base:
            r = m[n]; master = r["master"]
            levels[n] = max(-36.0, min(6.0, round(master + min(-16.0 - r["loud_db"], -1.5 - r["peak_db"]), 1)))
        m = run(cur(), levels)
    json.dump({n: t for n, t in tuned.items() if t}, open(os.path.join(HERE, "tuning.json"), "w"), indent=1)
    json.dump(levels, open(os.path.join(HERE, "levels.json"), "w"), indent=1)
    json.dump(m, open("/tmp/final_metrics.json", "w"))
    print("done")

if __name__ == "__main__":
    main()
