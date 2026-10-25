// Offline effects test: stability at extreme settings, CPU, tails decay
#include "../Source/DSP/SynthEngine.h"
#include "../Source/DSP/Effects.h"
#include <cstdio>
#include <chrono>
using namespace halcyon;
int main()
{
    const int sr = 48000, block = 256;
    Engine e; e.prepare (sr);
    Params p; p.osc[0].voices = 4; e.setParams (p);
    FxChain fxc; fxc.prepare (sr);
    FxParams f;
    f.distOn = f.phOn = f.chOn = f.eqOn = f.dlOn = f.rvOn = f.cpOn = true;
    f.phFeedback = 0.95f; f.dlFeedback = 0.95f; f.rvSize = 1.0f; f.rvDamp = 0.0f; f.eqLowDb = 15; f.eqHighDb = 15; f.distAmount = 1.0f;
    for (int type = 0; type < 4; ++type)
    {
        f.distType = type; fxc.reset();
        std::vector<float> L (block), R (block);
        e.noteOn (48, 1.0f);
        float peak = 0, tail = 0; bool bad = false;
        auto t0 = std::chrono::steady_clock::now();
        const int blocks = sr * 12 / block;
        for (int b = 0; b < blocks; ++b)
        {
            if (b == sr / block) e.noteOff (48);
            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            e.render (L.data(), R.data(), block);
            fxc.process (L.data(), R.data(), block, f, 128.0);
            for (int i = 0; i < block; ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) bad = true; peak = std::max (peak, std::abs (L[i])); if (b > blocks - 20) tail = std::max (tail, std::abs (L[i])); }
        }
        const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("dist type %d, all FX extreme: peak %.2f, level after 11 s %.4f, nan %d, CPU %.1f%%\n", type, peak, tail, (int) bad, el / 12 * 100);
    }
    // normal settings: delay tail must decay
    FxParams n; n.dlOn = n.rvOn = true; FxChain c2; c2.prepare (sr);
    std::vector<float> L (block), R (block); float tail = 0;
    e.noteOn (60, 1.0f);
    for (int b = 0; b < sr * 10 / block; ++b)
    {
        if (b == 20) e.noteOff (60);
        std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
        e.render (L.data(), R.data(), block); c2.process (L.data(), R.data(), block, n, 128.0);
        if (b > sr * 9 / block) for (float x : L) tail = std::max (tail, std::abs (x));
    }
    std::printf ("default delay+reverb: level after 9 s %.6f (should be ~0)\n", tail);
}
