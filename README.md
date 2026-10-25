# Halcyon — polyphonic synth (VST3 / AU / Standalone)

An original subtractive polysynth built with JUCE. Stage 1: the core synth.

![Halcyon](screenshot.png)

## Architecture
- **4 oscillators in two parts**: A1 + A2 feed Filter A, B1 + B2 feed Filter B
- Each oscillator: saw, pulse, triangle, sine, noise; octave, semitone, fine; volume, pan;
  **up to 8 unison voices** with detune and automatic stereo spread; free-running or retriggered phase
- **2 filters**: lowpass / bandpass / highpass, 12 or 24 dB, cutoff, resonance, drive, key tracking
  (zero-delay-feedback state-variable design)
- **Amp envelope per part** (ADSR)
- **2 mod envelopes and 2 LFOs** (sine, triangle, saw, square, S&H), each with amount and a target:
  cutoff A / B / both, pitch all / A / B, resonance, pan, volume
- **16-voice polyphony**, Poly / Mono / Legato modes, portamento (also in poly), pitch bend range
- On-screen keyboard; 6 factory presets (Init, Lead - Supersaw, Pluck - Trance, Pad - Warm,
  Bass - Reese, Stab - Melodic); everything saves with your project

Worst case (16 voices x 4 oscillators x 8 unison = 512 oscillators) uses about 25% of one CPU core.

## Roadmap
- Stage 2: effects (distortion, phaser, chorus, EQ, delay, reverb, compressor)
- Stage 3: arpeggiator, mod matrix, more mod sources
- Stage 4: preset import (needs reference preset files from a licensed copy of the original synth)

## Build
Push to GitHub and the included Actions workflow builds Windows, macOS and Linux, or:
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Engine-only test (no JUCE): `g++ -O2 -std=c++17 tests/engine_test.cpp -o et && ./et`

## License note
JUCE 8 is AGPLv3 or commercially licensed (free tier for small developers).
