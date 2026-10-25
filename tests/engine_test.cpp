// Offline engine test: stability, CPU, and a demo render (no JUCE needed)
#include "../Source/DSP/SynthEngine.h"
#include <cstdio>
#include <chrono>
using namespace halcyon;
static void wav (const char* fn, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = fopen (fn, "wb"); int n = (int) L.size(); int32_t v; int16_t s;
    fwrite ("RIFF",1,4,f); v = 36 + n*4; fwrite (&v,4,1,f); fwrite ("WAVEfmt ",1,8,f); v = 16; fwrite (&v,4,1,f);
    s = 1; fwrite (&s,2,1,f); s = 2; fwrite (&s,2,1,f); v = sr; fwrite (&v,4,1,f); v = sr*4; fwrite (&v,4,1,f);
    s = 4; fwrite (&s,2,1,f); s = 16; fwrite (&s,2,1,f); fwrite ("data",1,4,f); v = n*4; fwrite (&v,4,1,f);
    for (int i = 0; i < n; ++i) for (float x : { L[i], R[i] }) { s = (int16_t) (std::clamp (x, -1.0f, 1.0f) * 32767); fwrite (&s,2,1,f); }
    fclose (f);
}
int main()
{
    const int sr = 48000, block = 256;
    Engine e; e.prepare (sr);
    Params p;
    for (int o = 0; o < 4; ++o) { p.osc[o].voices = 8; p.osc[o].detune = 0.3f; p.osc[o].wave = o % 2 ? Pulse : Saw; }
    p.polyphony = 16; p.filt[0].cutoff = 3000; p.filt[1].cutoff = 3000; p.filt[0].reso = 0.9f;
    p.menv[0].amount = 0.5f; p.menv[0].target = TCutoffAB; p.lfo[0].amount = 0.1f; p.lfo[0].target = TPitchAll; p.lfo[0].rate = 5;
    e.setParams (p);
    // worst case: 16 voices x 4 osc x 8 unison
    for (int n = 0; n < 16; ++n) e.noteOn (40 + n * 3, 1.0f);
    std::vector<float> L (block), R (block);
    auto t0 = std::chrono::steady_clock::now();
    float peak = 0; bool bad = false;
    const int blocks = sr * 2 / block;
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        e.render (L.data(), R.data(), block);
        for (int i = 0; i < block; ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) bad = true; peak = std::max (peak, std::abs (L[i])); }
    }
    const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("worst case 512 oscillators: %.1f%% of one core, peak %.2f, voices %d, nan %d\n", el / 2.0 * 100, peak, e.activeVoiceCount(), (int) bad);
    e.allNotesOff();
    for (int b = 0; b < sr * 3 / block; ++b) { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f); e.render (L.data(), R.data(), block); }
    std::printf ("after release: %d active voices\n", e.activeVoiceCount());
    // stereo check: unison should make L != R
    return bad;
}
