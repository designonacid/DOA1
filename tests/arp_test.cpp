// Arpeggiator logic test (no JUCE)
#include "../Source/DSP/Arpeggiator.h"
#include <cstdio>
#include <string>
using namespace halcyon;
int main()
{
    const char* modes[] = { "Up", "Down", "Up/Down", "As Played", "Random", "Chord" };
    for (int mode = 0; mode < 6; ++mode)
    {
        Arpeggiator a; a.reset();
        ArpParams p; p.on = true; p.mode = mode; p.rate = 3; p.octaves = 2; p.gate = 0.5f;
        a.noteOn (64, 0.8f, false); a.noteOn (60, 0.8f, false); a.noteOn (67, 0.8f, false);   // played E C G
        int ons = 0, offs = 0; std::string seq; int lastOnSample = -1, minGap = 1 << 30;
        const double fs = 48000, bpm = 120; const int block = 256;
        for (int b = 0; b * block < fs * 2; ++b)   // 2 s = 4 beats = 16 sixteenths
            for (auto& e : a.process (block, fs, bpm, false, 0.0, p))
            {
                if (e.on) { ++ons; if (ons <= 12) seq += std::to_string (e.note) + " "; const int abs = b * block + e.sample;
                            if (lastOnSample >= 0 && abs != lastOnSample) minGap = std::min (minGap, abs - lastOnSample); lastOnSample = abs; }
                else ++offs;
            }
        std::printf ("%-9s on %2d off %2d  step %4d samples  first notes: %s\n", modes[mode], ons, offs, minGap, seq.c_str());
    }
    // swing: even/odd gaps differ
    Arpeggiator a; ArpParams p; p.on = true; p.rate = 3; p.swing = 0.3f; a.noteOn (60, 1, false);
    std::vector<int> times; for (int b = 0; b < 80; ++b) for (auto& e : a.process (256, 48000, 120, false, 0, p)) if (e.on) times.push_back (b * 256 + e.sample);
    std::printf ("swing 30%%: gaps %d %d %d %d (1/16 = 6000)\n", times[1]-times[0], times[2]-times[1], times[3]-times[2], times[4]-times[3]);
    // hold: release keys, arp keeps going
    Arpeggiator h; ArpParams hp; hp.on = true; hp.hold = true; h.noteOn (60, 1, true); h.noteOff (60, true);
    int n = 0; for (int b = 0; b < 100; ++b) for (auto& e : h.process (256, 48000, 120, false, 0, hp)) n += e.on;
    std::printf ("hold after release: %d notes in 0.53 s (expect ~5)\n", n);
}
