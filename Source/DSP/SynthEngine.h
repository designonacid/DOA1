#pragma once
// Halcyon synth engine: 4 oscillators in two parts (A1+A2 -> filter A, B1+B2 -> filter B),
// up to 8 unison voices per oscillator, TPT state-variable filters, ADSR envelopes,
// 2 mod envelopes and 2 LFOs. JUCE-independent so it can be tested offline.

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>

namespace halcyon
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kNumOsc = 4, kMaxUnison = 8, kMaxVoices = 16, kCtrl = 16; // control-rate block

enum Wave { Saw, Pulse, Tri, Sine, Noise };
enum FilterType { LowPass, BandPass, HighPass };
enum Target { TNone, TCutoffA, TCutoffB, TCutoffAB, TPitchAll, TPitchA, TPitchB, TResoAB, TPan, TVolume };
enum Mode { Poly, Mono, Legato };

struct ADSR { float a = 0.005f, d = 0.3f, s = 0.8f, r = 0.3f; };
struct OscParams { bool on = true; int wave = Saw, oct = 0, semi = 0; float fine = 0, vol = 0.7f, pan = 0; int voices = 1; float detune = 0.2f; bool retrig = false; };
struct FilterParams { int type = LowPass, slope = 1; float cutoff = 8000, reso = 0.1f, drive = 0, keytrack = 0; };
struct ModEnvParams { ADSR env { 0.001f, 0.3f, 0.0f, 0.3f }; float amount = 0; int target = TNone; };
struct LfoParams { int wave = 0; float rate = 1.0f, amount = 0; int target = TNone; };

struct Params
{
    OscParams osc[kNumOsc];
    FilterParams filt[2];
    ADSR amp[2];
    ModEnvParams menv[2];
    LfoParams lfo[2];
    int polyphony = 8, mode = Poly;
    float porta = 0.0f, masterDb = -6.0f, bendRange = 2.0f;
};

//==============================================================================
struct EnvCoefs { float attackInc, decayCoef, sustain, releaseCoef; };

inline EnvCoefs makeCoefs (const ADSR& p, double fs)
{
    auto c = [fs] (float sec) { return (float) std::exp (-4.6 / (std::max (0.0005f, sec) * fs)); }; // ~99% in `sec`
    return { (float) (1.0 / (std::max (0.0005f, p.a) * fs)), c (p.d), p.s, c (p.r) };
}

struct Envelope
{
    enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
    float v = 0.0f;
    void gateOn()  { stage = Attack; }   // starts from the current level: no clicks on retrigger
    void gateOff() { if (stage != Idle) stage = Release; }
    bool active() const { return stage != Idle; }
    float next (const EnvCoefs& c)
    {
        switch (stage)
        {
            case Attack:  v += c.attackInc; if (v >= 1.0f) { v = 1.0f; stage = Decay; } break;
            case Decay:   v = c.sustain + (v - c.sustain) * c.decayCoef; if (std::abs (v - c.sustain) < 1e-4f) stage = Sustain; break;
            case Sustain: v = c.sustain; break;
            case Release: v *= c.releaseCoef; if (v < 1e-5f) { v = 0.0f; stage = Idle; } break;
            case Idle: break;
        }
        return v;
    }
};

//==============================================================================
// Cytomic / Simper TPT state-variable filter
struct SVF
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    float process (float v0, float a1, float a2, float a3, float k, int type)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return type == LowPass ? v2 : type == BandPass ? v1 : v0 - k * v1 - v2;
    }
};
struct SVFCoefs { float a1, a2, a3, k; };
inline SVFCoefs svfCoefs (double fc, float k, double fs)
{
    const float g = (float) std::tan (kPi * std::clamp (fc, 15.0, fs * 0.49) / fs);
    const float a1 = 1.0f / (1.0f + g * (g + k));
    return { a1, g * a1, g * g * a1, k };
}

inline double polyBlep (double t, double dt)
{
    if (t < dt)       { t /= dt;            return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

//==============================================================================
struct Voice
{
    int note = -1;
    float velocity = 1.0f;
    bool gate = false;
    uint64_t age = 0;
    float pitch = 60.0f, targetPitch = 60.0f;  // semitones, with portamento
    double phase[kNumOsc][kMaxUnison] {};
    Envelope amp[2], mod[2];
    SVF filt[2][2][2];       // part, stage, channel
    uint32_t seed = 22222u;

    bool active() const { return amp[0].active() || amp[1].active(); }
};

//==============================================================================
class Engine
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& v : voices) v = Voice();
        for (int i = 0; i < kMaxVoices; ++i) voices[(size_t) i].seed = 12345u + 7919u * (uint32_t) i;
        heldNotes.clear();
        lfoPhase[0] = lfoPhase[1] = 0.0;
    }

    void setParams (const Params& p) { params = p; }
    void pitchBend (float normalised) { bend = normalised; } // -1..1

    void noteOn (int note, float vel)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);

        if (params.mode != Poly)
        {
            Voice& v = voices[0];
            const bool legato = params.mode == Legato && v.gate;
            startVoice (v, note, vel, ! legato);
            return;
        }
        // reuse a voice already playing this note, else a free one, else steal the oldest
        Voice* slot = nullptr;
        for (int i = 0; i < params.polyphony; ++i) if (voices[(size_t) i].note == note && voices[(size_t) i].active()) slot = &voices[(size_t) i];
        if (! slot) for (int i = 0; i < params.polyphony; ++i) if (! voices[(size_t) i].active()) { slot = &voices[(size_t) i]; break; }
        if (! slot)
        {
            slot = &voices[0];
            for (int i = 0; i < params.polyphony; ++i)
            {
                auto& v = voices[(size_t) i];
                const bool better = (! v.gate && slot->gate) || (v.gate == slot->gate && v.age < slot->age);
                if (better) slot = &v;
            }
        }
        startVoice (*slot, note, vel, true);
    }

    void noteOff (int note)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        if (params.mode != Poly)
        {
            Voice& v = voices[0];
            if (v.note != note) return;
            if (! heldNotes.empty()) startVoice (v, heldNotes.back(), v.velocity, params.mode == Mono); // glide back
            else release (v);
            return;
        }
        for (auto& v : voices) if (v.note == note && v.gate) release (v);
    }

    void allNotesOff() { heldNotes.clear(); for (auto& v : voices) release (v); }

    int activeVoiceCount() const { int n = 0; for (auto& v : voices) n += v.active() ? 1 : 0; return n; }

    // Render n samples, adding into L/R
    void render (float* L, float* R, int n)
    {
        prepareBlock();
        int pos = 0;
        while (pos < n)
        {
            const int len = std::min (kCtrl, n - pos);
            // ---- global LFOs at control rate ----
            for (int l = 0; l < 2; ++l)
            {
                lfoPhase[l] += params.lfo[l].rate * len / fs;
                if (lfoPhase[l] >= 1.0) { lfoPhase[l] -= std::floor (lfoPhase[l]); lfoSH[l] = nextRandom (lfoSeed) ; }
                lfoValue[l] = lfoShape (params.lfo[l].wave, lfoPhase[l], lfoSH[l]) * params.lfo[l].amount;
            }
            for (auto& v : voices)
                if (v.active()) renderVoice (v, L + pos, R + pos, len);
            pos += len;
        }
        const float g = std::pow (10.0f, params.masterDb / 20.0f);
        for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; }
    }

private:
    static float nextRandom (uint32_t& s) { s = s * 1664525u + 1013904223u; return ((s >> 9) * (1.0f / 8388608.0f)) * 2.0f - 1.0f; } // [-1, 1)

    static float lfoShape (int wave, double ph, float sh)
    {
        switch (wave)
        {
            case 1:  return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);   // triangle
            case 2:  return (float) (1.0 - 2.0 * ph);                                // saw down
            case 3:  return ph < 0.5 ? 1.0f : -1.0f;                                  // square
            case 4:  return sh;                                                      // sample & hold
            default: return (float) std::sin (2.0 * kPi * ph);                       // sine
        }
    }

    void release (Voice& v)
    {
        v.gate = false;
        for (auto& e : v.amp) e.gateOff();
        for (auto& e : v.mod) e.gateOff();
    }

    void startVoice (Voice& v, int note, float vel, bool retrigger)
    {
        const bool wasActive = v.active();
        v.targetPitch = (float) note;
        if (params.porta < 0.001f || (! wasActive && lastNote < 0)) v.pitch = (float) note;
        else if (! wasActive) v.pitch = (float) lastNote;   // poly portamento from the previous note
        lastNote = note;
        v.note = note;
        v.gate = true;
        v.age = ++ageCounter;
        if (retrigger || ! wasActive)
        {
            v.velocity = vel;
            for (auto& e : v.amp) e.gateOn();
            for (auto& e : v.mod) e.gateOn();
            if (! wasActive) for (auto& p : v.filt) for (auto& s : p) for (auto& f : s) f.reset();
            for (int o = 0; o < kNumOsc; ++o)
                for (int u = 0; u < kMaxUnison; ++u)
                    if (params.osc[o].retrig) v.phase[o][u] = 0.0;
                    else if (! wasActive)     v.phase[o][u] = (nextRandom (v.seed) + 1.0f) * 0.5f;
        }
    }

    void prepareBlock()
    {
        for (int p = 0; p < 2; ++p) ampC[p] = makeCoefs (params.amp[p], fs);
        for (int m = 0; m < 2; ++m) modC[m] = makeCoefs (params.menv[m].env, fs);

        for (int o = 0; o < kNumOsc; ++o)
        {
            const auto& op = params.osc[o];
            const int n = std::clamp (op.voices, 1, kMaxUnison);
            for (int u = 0; u < n; ++u)
            {
                const float spread = n == 1 ? 0.0f : (float) u / (float) (n - 1) * 2.0f - 1.0f;
                detuneSemis[o][u] = op.detune * 0.5f * spread;   // up to +/- 50 cents
                const float pan = std::clamp (op.pan + spread * 0.85f * std::min (1.0f, op.detune * 4.0f), -1.0f, 1.0f);
                const float gain = op.vol / std::sqrt ((float) n);
                gainL[o][u] = gain * std::cos ((pan + 1.0f) * (float) kPi * 0.25f);
                gainR[o][u] = gain * std::sin ((pan + 1.0f) * (float) kPi * 0.25f);
            }
        }
        portaCoef = params.porta < 0.001f ? 1.0f : 1.0f - std::exp (-(float) kCtrl / (params.porta * 0.4f * (float) fs));
    }

    float modSum (const Voice& v, int target) const
    {
        float s = 0.0f;
        for (int m = 0; m < 2; ++m) if (params.menv[m].target == target) s += v.mod[m].v * params.menv[m].amount;
        for (int l = 0; l < 2; ++l) if (params.lfo[l].target == target) s += lfoValue[l];
        return s;
    }

    void renderVoice (Voice& v, float* L, float* R, int len)
    {
        // ---- control rate ----
        v.pitch += (v.targetPitch - v.pitch) * portaCoef;
        const float pitchAll = modSum (v, TPitchAll) * 24.0f + bend * params.bendRange;
        const float pitchPart[2] = { pitchAll + modSum (v, TPitchA) * 24.0f, pitchAll + modSum (v, TPitchB) * 24.0f };
        const float cutAB = modSum (v, TCutoffAB);
        const float cutMod[2] = { cutAB + modSum (v, TCutoffA), cutAB + modSum (v, TCutoffB) };
        const float resoMod = modSum (v, TResoAB);
        const float panMod = modSum (v, TPan);
        const float volMod = std::clamp (1.0f + modSum (v, TVolume), 0.0f, 2.0f);

        double inc[kNumOsc][kMaxUnison];
        for (int o = 0; o < kNumOsc; ++o)
        {
            const auto& op = params.osc[o];
            if (! op.on) continue;
            const float base = v.pitch + op.oct * 12.0f + op.semi + op.fine * 0.01f + pitchPart[o / 2];
            for (int u = 0; u < std::clamp (op.voices, 1, kMaxUnison); ++u)
                inc[o][u] = 440.0 * std::pow (2.0, (base + detuneSemis[o][u] - 69.0) / 12.0) / fs;
        }

        SVFCoefs fc[2][2];
        for (int p = 0; p < 2; ++p)
        {
            const auto& f = params.filt[p];
            const double hz = f.cutoff * std::pow (2.0, cutMod[p] * 6.0 + (v.pitch - 60.0) / 12.0 * f.keytrack);
            const float res = std::clamp (f.reso + resoMod, 0.0f, 1.0f);
            const float k = 2.0f - 1.96f * res;
            if (f.slope == 1) { fc[p][0] = svfCoefs (hz, 1.414f, fs); fc[p][1] = svfCoefs (hz, k, fs); }
            else              { fc[p][0] = svfCoefs (hz, k, fs); }
        }
        const float velGain = 0.35f + 0.65f * v.velocity;
        const float panL = std::cos ((std::clamp (panMod, -1.0f, 1.0f) + 1.0f) * (float) kPi * 0.25f) * 1.4142f;
        const float panR = std::sin ((std::clamp (panMod, -1.0f, 1.0f) + 1.0f) * (float) kPi * 0.25f) * 1.4142f;

        // ---- audio rate ----
        for (int i = 0; i < len; ++i)
        {
            for (int m = 0; m < 2; ++m) v.mod[m].next (modC[m]);
            float outL = 0, outR = 0;
            for (int p = 0; p < 2; ++p)
            {
                const float env = v.amp[p].next (ampC[p]);
                const auto& oa = params.osc[p * 2];
                const auto& ob = params.osc[p * 2 + 1];
                if (env <= 0.0f || (! oa.on && ! ob.on)) continue;

                float sl = 0, sr = 0;
                for (int o = p * 2; o < p * 2 + 2; ++o)
                {
                    const auto& op = params.osc[o];
                    if (! op.on) continue;
                    const int n = std::clamp (op.voices, 1, kMaxUnison);
                    for (int u = 0; u < n; ++u)
                    {
                        double& ph = v.phase[o][u];
                        const double dt = inc[o][u];
                        float s;
                        switch (op.wave)
                        {
                            case Saw:   s = (float) (2.0 * ph - 1.0 - polyBlep (ph, dt)); break;
                            case Pulse: s = (ph < 0.5 ? 1.0f : -1.0f) + (float) (polyBlep (ph, dt) - polyBlep (std::fmod (ph + 0.5, 1.0), dt)); break;
                            case Tri:   s = (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph); break;
                            case Sine:  s = (float) std::sin (2.0 * kPi * ph); break;
                            default:    s = nextRandom (v.seed); break;
                        }
                        ph += dt;
                        if (ph >= 1.0) ph -= 1.0;
                        sl += s * gainL[o][u];
                        sr += s * gainR[o][u];
                    }
                }

                const auto& fp = params.filt[p];
                if (fp.drive > 0.001f)
                {
                    const float g = 1.0f + fp.drive * 9.0f, norm = 1.0f / std::sqrt (g);
                    sl = std::tanh (sl * g) * norm; sr = std::tanh (sr * g) * norm;
                }
                const auto& c0 = fc[p][0];
                sl = v.filt[p][0][0].process (sl, c0.a1, c0.a2, c0.a3, c0.k, fp.type);
                sr = v.filt[p][0][1].process (sr, c0.a1, c0.a2, c0.a3, c0.k, fp.type);
                if (fp.slope == 1)
                {
                    const auto& c1 = fc[p][1];
                    sl = v.filt[p][1][0].process (sl, c1.a1, c1.a2, c1.a3, c1.k, fp.type);
                    sr = v.filt[p][1][1].process (sr, c1.a1, c1.a2, c1.a3, c1.k, fp.type);
                }
                const float g = env * velGain * volMod;
                outL += sl * g; outR += sr * g;
            }
            L[i] += outL * panL;
            R[i] += outR * panR;
        }
        if (! v.active()) v.note = -1;
    }

    double fs = 44100.0;
    Params params;
    std::array<Voice, kMaxVoices> voices;
    std::vector<int> heldNotes;
    int lastNote = -1;
    uint64_t ageCounter = 0;
    float bend = 0.0f;

    EnvCoefs ampC[2] {}, modC[2] {};
    float detuneSemis[kNumOsc][kMaxUnison] {}, gainL[kNumOsc][kMaxUnison] {}, gainR[kNumOsc][kMaxUnison] {};
    float portaCoef = 1.0f;
    double lfoPhase[2] {};
    float lfoValue[2] {}, lfoSH[2] {};
    uint32_t lfoSeed = 987654u;
};

} // namespace halcyon
