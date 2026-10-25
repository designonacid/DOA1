#pragma once
// Halcyon effects rack, in signal order:
//   Distortion -> Phaser -> Chorus -> EQ -> Delay -> Reverb -> Compressor
// JUCE-independent. All effects are stereo.

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

namespace halcyon
{

struct FxParams
{
    bool distOn = false; int distType = 0; float distAmount = 0.3f, distMix = 1.0f;
    bool phOn = false;   float phRate = 0.3f, phDepth = 0.6f, phFeedback = 0.4f, phMix = 0.5f;
    bool chOn = false;   float chRate = 0.6f, chDepth = 0.4f, chDelayMs = 12.0f, chMix = 0.4f;
    bool eqOn = false;   float eqLowDb = 0.0f, eqHighDb = 0.0f;
    bool dlOn = false;   int dlTime = 3; float dlFeedback = 0.35f; bool dlPingPong = true; float dlToneHz = 6000.0f, dlMix = 0.25f;
    bool rvOn = false;   float rvSize = 0.7f, rvDamp = 0.5f, rvWidth = 1.0f, rvMix = 0.25f;
    bool cpOn = false;   float cpThreshDb = -12.0f, cpRatio = 4.0f, cpAttackMs = 5.0f, cpReleaseMs = 120.0f, cpMakeupDb = 0.0f;
};

// delay note values, in beats (quarter notes)
inline constexpr float kDelayBeats[] = { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f };
inline constexpr const char* kDelayNames[] = { "1/16", "1/8 T", "1/8", "1/8 D", "1/4 T", "1/4", "1/4 D", "1/2" };

//==============================================================================
namespace fx
{
constexpr double pi = 3.14159265358979323846;

struct Shelf   // RBJ shelving biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
    void set (double fs, double f, double db, bool high)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f / fs, cs = std::cos (w);
        const double al = std::sin (w) / 2.0 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        double nb0, nb1, nb2, na0, na1, na2;
        if (! high) { nb0 = A * ((A + 1) - (A - 1) * cs + sA); nb1 = 2 * A * ((A - 1) - (A + 1) * cs); nb2 = A * ((A + 1) - (A - 1) * cs - sA);
                      na0 = (A + 1) + (A - 1) * cs + sA;       na1 = -2 * ((A - 1) + (A + 1) * cs);     na2 = (A + 1) + (A - 1) * cs - sA; }
        else        { nb0 = A * ((A + 1) + (A - 1) * cs + sA); nb1 = -2 * A * ((A - 1) + (A + 1) * cs); nb2 = A * ((A + 1) + (A - 1) * cs - sA);
                      na0 = (A + 1) - (A - 1) * cs + sA;       na1 = 2 * ((A - 1) - (A + 1) * cs);      na2 = (A + 1) - (A - 1) * cs - sA; }
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0); a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
    float process (float x, int ch)
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
};

struct DelayLine
{
    std::vector<float> buf; int w = 0;
    void init (int size) { buf.assign ((size_t) std::max (4, size), 0.0f); w = 0; }
    void push (float x) { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
    float read (double delaySamples) const   // linear interpolation, delay >= 1
    {
        const int n = (int) buf.size();
        double rp = w - 1 - delaySamples;
        while (rp < 0) rp += n;
        const int i0 = (int) rp, i1 = (i0 + 1) % n;
        const float f = (float) (rp - i0);
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * f;
    }
};

// Freeverb-style comb / allpass (public-domain algorithm by Jezar)
struct Comb
{
    std::vector<float> buf; int idx = 0; float store = 0;
    void init (int n) { buf.assign ((size_t) n, 0.0f); idx = 0; store = 0; }
    float process (float in, float fb, float damp)
    {
        const float out = buf[(size_t) idx];
        store = out * (1.0f - damp) + store * damp;
        buf[(size_t) idx] = in + store * fb;
        if (++idx >= (int) buf.size()) idx = 0;
        return out;
    }
};
struct Allpass
{
    std::vector<float> buf; int idx = 0;
    void init (int n) { buf.assign ((size_t) n, 0.0f); idx = 0; }
    float process (float in)
    {
        const float b = buf[(size_t) idx];
        buf[(size_t) idx] = in + b * 0.5f;
        if (++idx >= (int) buf.size()) idx = 0;
        return b - in;
    }
};
} // namespace fx

//==============================================================================
class FxChain
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : chorusLine) d.init ((int) (fs * 0.06));
        for (auto& d : delayLine)  d.init ((int) (fs * 4.1));      // 1/2 note at 30 BPM
        const double sc = fs / 44100.0;
        const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, apT[4] = { 556, 441, 341, 225 };
        for (int ch = 0; ch < 2; ++ch)
        {
            const int spread = ch == 0 ? 0 : 23;
            for (int i = 0; i < 8; ++i) combs[ch][i].init ((int) ((combT[i] + spread) * sc));
            for (int i = 0; i < 4; ++i) aps[ch][i].init ((int) ((apT[i] + spread) * sc));
        }
        reset();
    }

    void reset()
    {
        for (auto& s : phState) for (auto& v : s) v = 0.0f;
        phFb[0] = phFb[1] = 0; phLfo = chLfo = 0;
        lowShelf.reset(); highShelf.reset();
        dlDamp[0] = dlDamp[1] = 0; dlSmoothed = -1.0;
        envDb = 0.0f; crushHold[0] = crushHold[1] = 0; crushCount = 0;
    }

    void process (float* L, float* R, int n, const FxParams& p, double bpm)
    {
        if (p.distOn) distortion (L, R, n, p);
        if (p.phOn)   phaser (L, R, n, p);
        if (p.chOn)   chorus (L, R, n, p);
        if (p.eqOn)   eq (L, R, n, p);
        if (p.dlOn)   delay (L, R, n, p, bpm);
        if (p.rvOn)   reverb (L, R, n, p);
        if (p.cpOn)   compressor (L, R, n, p);
    }

private:
    //--------------------------------------------------------------------------
    void distortion (float* L, float* R, int n, const FxParams& p)
    {
        const float a = p.distAmount, g = 1.0f + a * a * 30.0f;
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                float& s = ch == 0 ? L[i] : R[i];
                const float dry = s;
                float y;
                switch (p.distType)
                {
                    case 1: y = std::clamp (s * g, -1.0f, 1.0f); break;                       // hard clip
                    case 2: { float v = s * (1.0f + a * 8.0f);                                // wavefold
                              for (int k = 0; k < 8 && (v > 1.0f || v < -1.0f); ++k) v = v > 1.0f ? 2.0f - v : -2.0f - v;
                              y = v; break; }
                    case 3: { // bit + sample-rate crush
                              if (ch == 0 && ++crushCount >= 1 + (int) (a * 24.0f)) crushCount = 0;
                              if (crushCount == 0) crushHold[ch] = s;
                              const float steps = std::pow (2.0f, 16.0f - a * 13.0f);
                              y = std::round (crushHold[ch] * steps) / steps; break; }
                    default: y = std::tanh (s * g) / std::max (1.0f, std::tanh (g) * 1.4f);    // soft
                }
                s = dry + (y - dry) * p.distMix;
            }
    }

    //--------------------------------------------------------------------------
    void phaser (float* L, float* R, int n, const FxParams& p)
    {
        float coef[2] = { 0, 0 };
        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0)
            {
                phLfo += p.phRate * 16.0 / fs;
                phLfo -= std::floor (phLfo);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double l = 0.5 + 0.5 * std::sin (2 * fx::pi * (phLfo + ch * 0.25));
                    const double f = 200.0 * std::pow (20.0, l * p.phDepth);      // 200 Hz .. 4 kHz
                    const double t = std::tan (fx::pi * std::min (f, fs * 0.45) / fs);
                    coef[ch] = (float) ((t - 1.0) / (t + 1.0));
                }
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                float& s = ch == 0 ? L[i] : R[i];
                float x = s + phFb[ch] * p.phFeedback;
                for (int k = 0; k < 6; ++k)
                {
                    const float y = coef[ch] * x + phState[ch][k];
                    phState[ch][k] = x - coef[ch] * y;
                    x = y;
                }
                phFb[ch] = x;
                s = s * (1.0f - p.phMix * 0.5f) + x * p.phMix * 0.5f;
            }
        }
    }

    //--------------------------------------------------------------------------
    void chorus (float* L, float* R, int n, const FxParams& p)
    {
        const double base = p.chDelayMs * 0.001 * fs, depth = p.chDepth * 0.006 * fs; // up to +/- 6 ms
        for (int i = 0; i < n; ++i)
        {
            chLfo += p.chRate / fs;
            if (chLfo >= 1.0) chLfo -= 1.0;
            const double mL = std::sin (2 * fx::pi * chLfo), mR = std::cos (2 * fx::pi * chLfo);
            const float inL = L[i], inR = R[i];
            chorusLine[0].push (inL); chorusLine[1].push (inR);
            const float wL = chorusLine[0].read (std::max (2.0, base + depth * mL));
            const float wR = chorusLine[1].read (std::max (2.0, base + depth * mR));
            L[i] = inL * (1.0f - p.chMix * 0.5f) + wL * p.chMix;
            R[i] = inR * (1.0f - p.chMix * 0.5f) + wR * p.chMix;
        }
    }

    //--------------------------------------------------------------------------
    void eq (float* L, float* R, int n, const FxParams& p)
    {
        if (p.eqLowDb != lastLow)   { lowShelf.set (fs, 120.0, p.eqLowDb, false);   lastLow = p.eqLowDb; }
        if (p.eqHighDb != lastHigh) { highShelf.set (fs, 5000.0, p.eqHighDb, true); lastHigh = p.eqHighDb; }
        for (int i = 0; i < n; ++i)
        {
            L[i] = highShelf.process (lowShelf.process (L[i], 0), 0);
            R[i] = highShelf.process (lowShelf.process (R[i], 1), 1);
        }
    }

    //--------------------------------------------------------------------------
    void delay (float* L, float* R, int n, const FxParams& p, double bpm)
    {
        const int idx = std::clamp (p.dlTime, 0, 7);
        const double target = std::min (fs * 4.0, kDelayBeats[idx] * 60.0 / std::max (30.0, bpm) * fs);
        if (dlSmoothed < 0) dlSmoothed = target;
        const float damp = (float) std::exp (-2 * fx::pi * p.dlToneHz / fs);
        for (int i = 0; i < n; ++i)
        {
            dlSmoothed += (target - dlSmoothed) * 0.0005;   // glide on tempo / time changes
            const float tapL = delayLine[0].read (dlSmoothed), tapR = delayLine[1].read (dlSmoothed);
            dlDamp[0] = tapL + (dlDamp[0] - tapL) * damp;
            dlDamp[1] = tapR + (dlDamp[1] - tapR) * damp;
            const float fbL = dlDamp[0] * p.dlFeedback, fbR = dlDamp[1] * p.dlFeedback;
            if (p.dlPingPong)
            {
                delayLine[0].push ((L[i] + R[i]) * 0.5f + fbR);   // input enters left, then bounces
                delayLine[1].push (fbL);
            }
            else
            {
                delayLine[0].push (L[i] + fbL);
                delayLine[1].push (R[i] + fbR);
            }
            L[i] += tapL * p.dlMix;
            R[i] += tapR * p.dlMix;
        }
    }

    //--------------------------------------------------------------------------
    void reverb (float* L, float* R, int n, const FxParams& p)
    {
        const float fb = 0.7f + p.rvSize * 0.28f, damp = p.rvDamp * 0.4f;
        const float w1 = (1.0f + p.rvWidth) * 0.5f, w2 = (1.0f - p.rvWidth) * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float in = (L[i] + R[i]) * 0.015f;
            float out[2] = { 0, 0 };
            for (int ch = 0; ch < 2; ++ch)
            {
                for (auto& c : combs[ch]) out[ch] += c.process (in, fb, damp);
                for (auto& a : aps[ch])   out[ch] = a.process (out[ch]);
            }
            const float wl = out[0] * w1 + out[1] * w2, wr = out[1] * w1 + out[0] * w2;
            L[i] = L[i] * (1.0f - p.rvMix * 0.5f) + wl * p.rvMix * 3.0f;
            R[i] = R[i] * (1.0f - p.rvMix * 0.5f) + wr * p.rvMix * 3.0f;
        }
    }

    //--------------------------------------------------------------------------
    void compressor (float* L, float* R, int n, const FxParams& p)
    {
        const float att = std::exp (-1.0f / (std::max (0.1f, p.cpAttackMs) * 0.001f * (float) fs));
        const float rel = std::exp (-1.0f / (std::max (5.0f, p.cpReleaseMs) * 0.001f * (float) fs));
        const float makeup = std::pow (10.0f, p.cpMakeupDb / 20.0f), knee = 6.0f;
        for (int i = 0; i < n; ++i)
        {
            const float lvl = std::max (std::abs (L[i]), std::abs (R[i]));
            const float db = 20.0f * std::log10 (std::max (lvl, 1e-6f));
            const float over = db - p.cpThreshDb;
            float gr;   // gain reduction in dB (>= 0), soft knee
            if (over <= -knee * 0.5f)     gr = 0.0f;
            else if (over >= knee * 0.5f) gr = over * (1.0f - 1.0f / p.cpRatio);
            else { const float k = over + knee * 0.5f; gr = (1.0f - 1.0f / p.cpRatio) * k * k / (2.0f * knee); }
            envDb = gr > envDb ? gr + (envDb - gr) * att : gr + (envDb - gr) * rel;
            const float g = std::pow (10.0f, -envDb / 20.0f) * makeup;
            L[i] *= g; R[i] *= g;
        }
    }

    double fs = 44100.0;
    // distortion
    float crushHold[2] {}; int crushCount = 0;
    // phaser
    float phState[2][6] {}, phFb[2] {}; double phLfo = 0;
    // chorus
    fx::DelayLine chorusLine[2]; double chLfo = 0;
    // eq
    fx::Shelf lowShelf, highShelf; float lastLow = -999.0f, lastHigh = -999.0f;
    // delay
    fx::DelayLine delayLine[2]; float dlDamp[2] {}; double dlSmoothed = -1.0;
    // reverb
    fx::Comb combs[2][8]; fx::Allpass aps[2][4];
    // compressor
    float envDb = 0.0f;
};

} // namespace halcyon
