// preset_lab: renders presets with the real Halcyon engine + FX + arp (no JUCE) and measures them.
// Input:  presets.tsv  (name <TAB> id=value,id=value,...  -- every parameter listed)
// Output: metrics.json, plus WAVs when given a wav directory.
// Build:  g++ -O2 -std=c++17 tools/preset_lab.cpp -o preset_lab
// Run:    ./preset_lab presets.tsv metrics.json [wavdir]

#include "../Source/DSP/SynthEngine.h"
#include "../Source/DSP/Effects.h"
#include "../Source/DSP/Arpeggiator.h"
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <complex>

using namespace halcyon;
constexpr double SR = 48000.0, BPM = 128.0, BEAT = 60.0 / BPM;
constexpr int BLOCK = 256;

struct Note { double t, len; int note; float vel; };
struct Part { std::vector<Note> notes; std::vector<std::pair<double, float>> wheel; double length; bool sustained; };

static std::string category (const std::string& name)
{
    auto p = name.find (" - ");
    return p == std::string::npos ? "General" : name.substr (0, p);
}

// A musical part per category: what a producer would actually play with this kind of sound.
static Part partFor (const std::string& category)
{
    // "Trance Bass" -> Bass, "Techno Stab" -> Stab; techno/trance leads play the hypnotic motif
    const auto sp = category.rfind (' ');
    std::string cat = sp == std::string::npos ? category : category.substr (sp + 1);
    if (cat == "Lead" && category != "Lead") cat = "Techno Lead";
    Part p; p.length = 8 * BEAT; p.sustained = false;
    auto n = [&] (double beat, double len, int note, float vel) { p.notes.push_back ({ beat * BEAT, len * BEAT, note, vel }); };
    const int am[3] = { 57, 60, 64 }, fm[3] = { 53, 57, 60 };
    if (cat == "Lead")
    {
        n (0, 1.4, 69, 0.95); n (1.5, 0.4, 72, 0.7); n (2, 0.9, 76, 0.9); n (3, 0.9, 74, 0.75);
        n (4, 0.4, 72, 0.9); n (4.5, 0.4, 71, 0.7); n (5, 0.9, 69, 0.85); n (6, 1.9, 76, 0.95);
        p.wheel = { { 6.2 * BEAT, 0.0f }, { 7.9 * BEAT, 1.0f } };          // vibrato swell on the held note
    }
    else if (cat == "Techno Lead")
    {
        const int mot[8] = { 69, 72, 76, 69, 79, 76, 72, 76 };
        for (int e = 0; e < 8; ++e) n (e * 0.5, 0.35, mot[e], e % 4 == 0 ? 0.95f : 0.7f);
        n (4, 0.35, 69, 0.95); n (4.5, 0.35, 72, 0.7); n (5, 0.35, 76, 0.8);
        n (6, 1.9, 74, 0.95);
        p.wheel = { { 6.2 * BEAT, 0.0f }, { 7.9 * BEAT, 1.0f } };
    }
    else if (cat == "Bass")
    {
        for (int e = 0; e < 4; ++e) n (e + 0.5, 0.4, 33, 0.85);                  // offbeat 8ths
        for (int s = 0; s < 16; ++s) if (s % 4) n (4 + s * 0.25, 0.2, 29, s % 4 == 2 ? 0.9f : 0.7f);  // rolling 16ths
    }
    else if (cat == "Pluck" || cat == "Keys")
    {
        for (int bar = 0; bar < 2; ++bar)
            for (int e = 0; e < 8; ++e)
            {
                if (e == 3 || e == 7) continue;
                for (int k = 0; k < 3; ++k) n (bar * 4 + e * 0.5, 0.4, (bar ? fm : am)[k] + 12, e % 2 ? 0.7f : 0.95f);
            }
    }
    else if (cat == "Stab")
    {
        for (int bar = 0; bar < 2; ++bar)
            for (int e = 0; e < 4; ++e)
                for (int k = 0; k < 3; ++k) n (bar * 4 + e + 0.5, 0.3, (bar ? fm : am)[k] + 12, 0.9f);
    }
    else if (cat == "FX") { n (0, 7.8, 60, 0.9); p.sustained = true; }
    else
    {   // pads, arps, init: held chords
        for (int bar = 0; bar < 2; ++bar) for (int k = 0; k < 3; ++k) n (bar * 4, 3.9, (bar ? fm : am)[k], 0.85f);
        p.sustained = true;
        if (cat == "Pad") p.wheel = { { 4 * BEAT, 0.0f }, { 7.9 * BEAT, 1.0f } };
    }
    return p;
}

struct Settings { Params p; FxParams f; ArpParams a; float master = -6; };

static Settings toSettings (std::map<std::string, float>& v)
{
    Settings s;
    auto g = [&] (const std::string& id) { auto it = v.find (id); if (it == v.end()) { std::fprintf (stderr, "missing %s\n", id.c_str()); return 0.0f; } return it->second; };
    const char* osc[4] = { "a1", "a2", "b1", "b2" };
    for (int o = 0; o < 4; ++o)
    {
        std::string pre = osc[o]; auto& op = s.p.osc[o];
        op.on = g (pre + "_on") > 0.5f; op.wave = (int) g (pre + "_wave"); op.oct = (int) g (pre + "_oct"); op.semi = (int) g (pre + "_semi");
        op.fine = g (pre + "_fine"); op.vol = g (pre + "_vol"); op.pan = g (pre + "_pan"); op.voices = (int) g (pre + "_voices");
        op.detune = g (pre + "_detune"); op.retrig = g (pre + "_retrig") > 0.5f;
    }
    const char* part[2] = { "a", "b" };
    for (int i = 0; i < 2; ++i)
    {
        std::string pre = part[i]; auto& f = s.p.filt[i];
        f.type = (int) g (pre + "_type"); f.slope = (int) g (pre + "_slope"); f.cutoff = g (pre + "_cutoff");
        f.reso = g (pre + "_reso"); f.drive = g (pre + "_drive"); f.keytrack = g (pre + "_key");
        s.p.amp[i] = { g (pre + "_a"), g (pre + "_d"), g (pre + "_s"), g (pre + "_r") };
        std::string m = "m" + std::to_string (i + 1), l = "l" + std::to_string (i + 1);
        s.p.menv[i].env = { g (m + "_a"), g (m + "_d"), g (m + "_s"), g (m + "_r") };
        s.p.menv[i].amount = g (m + "_amt"); s.p.menv[i].target = (int) g (m + "_target");
        s.p.lfo[i] = { (int) g (l + "_wave"), g (l + "_rate"), g (l + "_amt"), (int) g (l + "_target") };
    }
    for (int m = 0; m < kNumSlots; ++m)
    {
        std::string pre = "mm" + std::to_string (m + 1);
        s.p.matrix[m] = { (int) g (pre + "_src"), (int) g (pre + "_dst"), g (pre + "_amt") };
    }
    s.p.mode = (int) g ("mode"); s.p.polyphony = (int) g ("poly"); s.p.porta = g ("porta"); s.p.bendRange = g ("bend");
    s.master = g ("master");
    auto& f = s.f;
    f.distOn = g ("dist_on") > 0.5f; f.distType = (int) g ("dist_type"); f.distAmount = g ("dist_amt"); f.distMix = g ("dist_mix");
    f.phOn = g ("ph_on") > 0.5f; f.phRate = g ("ph_rate"); f.phDepth = g ("ph_depth"); f.phFeedback = g ("ph_fb"); f.phMix = g ("ph_mix");
    f.chOn = g ("ch_on") > 0.5f; f.chRate = g ("ch_rate"); f.chDepth = g ("ch_depth"); f.chDelayMs = g ("ch_delay"); f.chMix = g ("ch_mix");
    f.eqOn = g ("eq_on") > 0.5f; f.eqLowDb = g ("eq_low"); f.eqHighDb = g ("eq_high");
    f.dlOn = g ("dl_on") > 0.5f; f.dlTime = (int) g ("dl_time"); f.dlFeedback = g ("dl_fb"); f.dlPingPong = g ("dl_pp") > 0.5f;
    f.dlToneHz = g ("dl_tone"); f.dlMix = g ("dl_mix");
    f.rvOn = g ("rv_on") > 0.5f; f.rvSize = g ("rv_size"); f.rvDamp = g ("rv_damp"); f.rvWidth = g ("rv_width"); f.rvMix = g ("rv_mix");
    f.cpOn = g ("cp_on") > 0.5f; f.cpThreshDb = g ("cp_thresh"); f.cpRatio = g ("cp_ratio"); f.cpAttackMs = g ("cp_att");
    f.cpReleaseMs = g ("cp_rel"); f.cpMakeupDb = g ("cp_makeup");
    auto& a = s.a;
    a.on = g ("arp_on") > 0.5f; a.mode = (int) g ("arp_mode"); a.rate = (int) g ("arp_rate"); a.octaves = (int) g ("arp_oct");
    a.gate = g ("arp_gate"); a.swing = g ("arp_swing"); a.hold = g ("arp_hold") > 0.5f;
    return s;
}

// Render exactly like the plugin: block-wise, MIDI -> arp -> engine, then FX, then master.
static void render (Settings& s, const Part& part, std::vector<float>& L, std::vector<float>& R, bool dryOnly)
{
    Engine e; e.prepare (SR); FxChain fx; fx.prepare (SR); Arpeggiator arp; arp.reset();
    FxParams f = s.f; if (dryOnly) { f.dlOn = false; f.rvOn = false; }
    const double tail = getenv ("LAB_TAIL") ? std::atof (getenv ("LAB_TAIL")) : 1.5;
    const int total = (int) ((part.length + tail) * SR);
    L.assign ((size_t) total, 0.0f); R.assign ((size_t) total, 0.0f);
    struct Ev { int sample; int note; float vel; bool on; };
    std::vector<Ev> evs;
    for (auto& nt : part.notes) { evs.push_back ({ (int) (nt.t * SR), nt.note, nt.vel, true }); evs.push_back ({ (int) ((nt.t + nt.len) * SR), nt.note, 0, false }); }
    std::stable_sort (evs.begin(), evs.end(), [] (auto& a, auto& b) { return a.sample < b.sample || (a.sample == b.sample && ! a.on && b.on); });
    size_t ei = 0;
    for (int pos = 0; pos < total; pos += BLOCK)
    {
        const int n = std::min (BLOCK, total - pos);
        // mod wheel (piecewise linear between points)
        float wheel = 0.0f;
        const double t = pos / SR;
        for (size_t k = 0; k + 1 < part.wheel.size(); ++k)
            if (t >= part.wheel[k].first) wheel = t >= part.wheel[k + 1].first ? part.wheel[k + 1].second
                : part.wheel[k].second + (part.wheel[k + 1].second - part.wheel[k].second) * (float) ((t - part.wheel[k].first) / (part.wheel[k + 1].first - part.wheel[k].first));
        e.modWheel (wheel);
        e.setParams (s.p);
        std::vector<Ev> block;
        while (ei < evs.size() && evs[ei].sample < pos + n) { auto ev = evs[ei++]; ev.sample -= pos; block.push_back (ev); }
        if (s.a.on)
        {
            for (auto& ev : block) { if (ev.on) arp.noteOn (ev.note, ev.vel, s.a.hold); else arp.noteOff (ev.note, s.a.hold); }
            block.clear();
            for (auto& ae : arp.process (n, SR, BPM, false, 0.0, s.a)) block.push_back ({ ae.sample, ae.note, ae.velocity, ae.on });
        }
        int at = 0;
        for (auto& ev : block)
        {
            if (ev.sample > at) { e.render (&L[(size_t) pos + at], &R[(size_t) pos + at], ev.sample - at); at = ev.sample; }
            if (ev.on) e.noteOn (ev.note, ev.vel); else e.noteOff (ev.note);
        }
        if (at < n) e.render (&L[(size_t) pos + at], &R[(size_t) pos + at], n - at);
        fx.process (&L[(size_t) pos], &R[(size_t) pos], n, f, BPM);
    }
    const float g = std::pow (10.0f, s.master / 20.0f);
    for (auto& x : L) x *= g;
    for (auto& x : R) x *= g;
}

static double rms (const std::vector<float>& L, const std::vector<float>& R, int a, int b)
{
    a = std::max (a, 0); b = std::min (b, (int) L.size()); if (b <= a) return 0;
    double s = 0; for (int i = a; i < b; ++i) s += 0.5 * (L[i] * L[i] + R[i] * R[i]); return std::sqrt (s / (b - a));
}

static double centroid (const std::vector<float>& L, int a, int n)
{
    if (a < 0 || a + n > (int) L.size()) return 0;
    // direct DFT on 1024 samples is too slow per call; use a 2048-point radix-2 FFT
    int N = 2048; std::vector<std::complex<double>> x ((size_t) N);
    for (int i = 0; i < N; ++i) x[(size_t) i] = i < n ? L[(size_t) (a + i)] * (0.5 - 0.5 * std::cos (2 * kPi * i / (n - 1))) : 0.0;
    for (int i = 1, j = 0; i < N; ++i) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (x[(size_t) i], x[(size_t) j]); }
    for (int len = 2; len <= N; len <<= 1)
    {
        const std::complex<double> w = std::polar (1.0, -2 * kPi / len);
        for (int i = 0; i < N; i += len) { std::complex<double> wn (1); for (int j = 0; j < len / 2; ++j) { auto u = x[(size_t) (i + j)], v = x[(size_t) (i + j + len / 2)] * wn; x[(size_t) (i + j)] = u + v; x[(size_t) (i + j + len / 2)] = u - v; wn *= w; } }
    }
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; ++k) { const double m = std::abs (x[(size_t) k]), f = k * SR / N; num += m * f; den += m; }
    return den > 0 ? num / den : 0;
}

static void bands (const std::vector<float>& L, std::vector<double>& out)
{
    // 24 log bands 50 Hz..16 kHz, energy summed over 2048-sample frames
    out.assign (24, 0.0);
    const int N = 2048;
    for (int a = 0; a + N <= (int) L.size(); a += N)
    {
        std::vector<std::complex<double>> x ((size_t) N);
        for (int i = 0; i < N; ++i) x[(size_t) i] = L[(size_t) (a + i)] * (0.5 - 0.5 * std::cos (2 * kPi * i / (N - 1)));
        for (int i = 1, j = 0; i < N; ++i) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (x[(size_t) i], x[(size_t) j]); }
        for (int len = 2; len <= N; len <<= 1)
        {
            const std::complex<double> w = std::polar (1.0, -2 * kPi / len);
            for (int i = 0; i < N; i += len) { std::complex<double> wn (1); for (int j = 0; j < len / 2; ++j) { auto u = x[(size_t) (i + j)], v = x[(size_t) (i + j + len / 2)] * wn; x[(size_t) (i + j)] = u + v; x[(size_t) (i + j + len / 2)] = u - v; wn *= w; } }
        }
        for (int k = 1; k < N / 2; ++k)
        {
            const double f = k * SR / N;
            if (f < 50 || f >= 16000) continue;
            const int b = std::min (23, (int) (std::log (f / 50.0) / std::log (16000.0 / 50.0) * 24));
            out[(size_t) b] += std::norm (x[(size_t) k]);
        }
    }
    double s = 0; for (double v : out) s += v; if (s > 0) for (double& v : out) v = std::log10 (v / s + 1e-9);
}

static void writeWav (const std::string& fn, const std::vector<float>& L, const std::vector<float>& R)
{
    FILE* f = std::fopen (fn.c_str(), "wb"); if (! f) return;
    const int n = (int) L.size(); int32_t v; int16_t s;
    std::fwrite ("RIFF", 1, 4, f); v = 36 + n * 4; std::fwrite (&v, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f); v = 16; std::fwrite (&v, 4, 1, f);
    s = 1; std::fwrite (&s, 2, 1, f); s = 2; std::fwrite (&s, 2, 1, f); v = (int) SR; std::fwrite (&v, 4, 1, f); v = (int) SR * 4; std::fwrite (&v, 4, 1, f);
    s = 4; std::fwrite (&s, 2, 1, f); s = 16; std::fwrite (&s, 2, 1, f); std::fwrite ("data", 1, 4, f); v = n * 4; std::fwrite (&v, 4, 1, f);
    for (int i = 0; i < n; ++i) for (float x : { L[(size_t) i], R[(size_t) i] }) { s = (int16_t) (std::clamp (x, -1.0f, 1.0f) * 32767); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: preset_lab presets.tsv metrics.json [wavdir]\n"); return 1; }
    std::ifstream in (argv[1]);
    FILE* out = std::fopen (argv[2], "w");
    const std::string wavdir = argc > 3 ? argv[3] : "";
    std::fprintf (out, "{\n");
    std::string line; bool first = true;
    while (std::getline (in, line))
    {
        const auto tab = line.find ('\t'); if (tab == std::string::npos) continue;
        const std::string name = line.substr (0, tab);
        std::map<std::string, float> vals;
        std::stringstream ss (line.substr (tab + 1)); std::string kv;
        while (std::getline (ss, kv, ',')) { auto eq = kv.find ('='); vals[kv.substr (0, eq)] = std::stof (kv.substr (eq + 1)); }
        Settings s = toSettings (vals);
        const auto cat = category (name);
        Part part = partFor (cat);
        if (const char* noteEnv = getenv ("LAB_NOTE"))
        {
            const double hold = getenv ("LAB_HOLD") ? std::atof (getenv ("LAB_HOLD")) : 1.0;
            part = Part(); part.notes = { { 0.0, hold, std::atoi (noteEnv), 0.9f } };
            part.length = hold; part.sustained = true;
        }
        std::vector<float> L, R, dL, dR;
        render (s, part, L, R, false);
        render (s, part, dL, dR, true);

        // ---- metrics ----
        bool finite = true; float peak = 0;
        for (size_t i = 0; i < L.size(); ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) finite = false; peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) }); }
        // loudness proxy: mean energy of the louder half of 50 ms frames
        std::vector<double> fr; const int fl = (int) (0.05 * SR);
        for (int a = 0; a + fl <= (int) (part.length * SR + SR); a += fl) { double r = rms (L, R, a, a + fl); fr.push_back (r * r); }
        std::sort (fr.begin(), fr.end()); double loud = 0; for (size_t i = fr.size() / 2; i < fr.size(); ++i) loud += fr[i];
        loud /= std::max<size_t> (1, fr.size() - fr.size() / 2);
        // wash: level in the gaps between notes (wet render) vs level while notes play (dry render)
        double noteE = 0, gapE = 0; int noteN = 0, gapN = 0;
        std::vector<char> held ((size_t) ((part.length + 0.5) * SR) + 1, 0);   // includes 0.5 s after the part
        for (auto& nt : part.notes) for (int i = (int) (nt.t * SR); i < (int) ((nt.t + nt.len + 0.06) * SR) && i < (int) held.size(); ++i) held[(size_t) i] = 1;
        for (int i = 0; i < (int) held.size(); i += 64)
        {
            const double r = rms (L, R, i, i + 64);
            if (held[(size_t) i]) { const double d = rms (dL, dR, i, i + 64); noteE += d * d; ++noteN; } else { gapE += r * r; ++gapN; }
        }
        if (getenv ("LAB_DEBUG")) std::fprintf (stderr, "%s: noteN %d gapN %d noteE %g gapE %g\n", name.c_str(), noteN, gapN, noteE, gapE);
        const double washDb = (gapN && noteN && noteE > 0) ? 10 * std::log10 ((gapE / gapN + 1e-12) / (noteE / noteN)) : -99;
        // pluck: brightness 5-40 ms vs 120-155 ms into each note longer than 0.15 s (dry render, first voice of chord)
        std::vector<double> ratios; double earlyC = 0; int ec = 0;
        double lastT = -1;
        for (auto& nt : part.notes)
        {
            if (nt.t == lastT || nt.len < 0.12) continue; lastT = nt.t;
            const int a = (int) (nt.t * SR);
            const double c1 = centroid (dL, a + 240, 1680), c2 = centroid (dL, a + 4320, 1680);   // 5-40 ms vs 90-125 ms
            if (c1 > 0 && c2 > 0) { ratios.push_back (c2 / c1); earlyC += c1; ++ec; }
        }
        std::sort (ratios.begin(), ratios.end());
        const double pluck = ratios.empty() ? 1.0 : ratios[ratios.size() / 2];
        double lr = 0, ll = 0, rr = 0; for (size_t i = 0; i < L.size(); ++i) { lr += L[i] * R[i]; ll += L[i] * L[i]; rr += R[i] * R[i]; }
        std::vector<double> bd; bands (dL, bd);

        std::fprintf (out, "%s \"%s\": {\"cat\": \"%s\", \"finite\": %d, \"peak_db\": %.2f, \"loud_db\": %.2f, \"wash_db\": %.1f, \"pluck\": %.3f, \"bright\": %.0f, \"corr\": %.3f, \"master\": %.2f, \"bands\": [",
                      first ? "" : ",\n", name.c_str(), cat.c_str(), (int) finite, 20 * std::log10 (peak + 1e-9), 10 * std::log10 (loud + 1e-12),
                      washDb, pluck, ec ? earlyC / ec : 0.0, lr / std::sqrt (ll * rr + 1e-12), s.master);
        for (size_t b = 0; b < bd.size(); ++b) std::fprintf (out, "%s%.3f", b ? "," : "", bd[b]);
        std::fprintf (out, "]}");
        first = false;
        if (! wavdir.empty()) writeWav (wavdir + "/" + name + ".wav", L, R);
    }
    std::fprintf (out, "\n}\n");
    std::fclose (out);
    return 0;
}
