#pragma once
// Tempo-synced arpeggiator. Feed it the notes you hold, call process() once per
// block, and it returns the note events to play in that block.

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace halcyon
{
struct ArpParams
{
    bool on = false;
    int mode = 0;        // Up, Down, Up/Down, As Played, Random, Chord
    int rate = 3;        // index into kArpBeats
    int octaves = 1;     // 1..4
    float gate = 0.5f;   // fraction of a step (1 = legato)
    float swing = 0.0f;  // 0..0.5, delays every second step
    bool hold = false;   // latch
};

inline constexpr float kArpBeats[] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f };
inline constexpr const char* kArpRateNames[] = { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" };

struct NoteEvent { int sample; int note; float velocity; bool on; };

class Arpeggiator
{
public:
    void reset() { held.clear(); playing.clear(); physicalDown = 0; lastStep = -1; stepCounter = 0; internalPpq = 0.0; offPending = false; }

    void noteOn (int note, float vel, bool hold)
    {
        if (hold && physicalDown == 0) held.clear();     // new chord replaces the latched one
        ++physicalDown;
        const bool wasEmpty = held.empty();
        remove (note);
        held.push_back ({ note, vel });
        if (wasEmpty) { internalPpq = 0.0; lastStep = -1; stepCounter = 0; }  // free-running clock restarts on first key
    }

    void noteOff (int note, bool hold)
    {
        physicalDown = std::max (0, physicalDown - 1);
        if (! hold) remove (note);
    }

    void releaseLatch() { if (physicalDown == 0) held.clear(); }

    // Returns this block's note events, sorted by sample.
    const std::vector<NoteEvent>& process (int n, double fs, double bpm, bool hostPlaying, double hostPpq, const ArpParams& p)
    {
        out.clear();
        const double ppqPerSample = bpm / (60.0 * fs);
        const double ppq0 = hostPlaying ? hostPpq : internalPpq;
        const double stepBeats = kArpBeats[std::clamp (p.rate, 0, 5)];
        const double swing = std::clamp ((double) p.swing, 0.0, 0.5);

        for (int i = 0; i < n; ++i)
        {
            const double ppq = ppq0 + i * ppqPerSample;

            if (offPending && ppq >= offAtPpq) { releaseAll (i); offPending = false; }

            // which step are we in (pairs of steps, second one delayed by swing)
            const double pairLen = stepBeats * 2.0;
            const double pairIdx = std::floor (ppq / pairLen);
            const double t = ppq - pairIdx * pairLen;
            const bool second = t >= stepBeats * (1.0 + swing);
            const long long step = (long long) pairIdx * 2 + (second ? 1 : 0);

            if (step != lastStep)
            {
                lastStep = step;
                releaseAll (i);
                offPending = false;
                if (! held.empty())
                {
                    const double stepStart = pairIdx * pairLen + (second ? stepBeats * (1.0 + swing) : 0.0);
                    const double stepLen = second ? stepBeats * (1.0 - swing) : stepBeats * (1.0 + swing);
                    for (auto& nt : nextNotes (p)) { out.push_back ({ i, nt.note, nt.vel, true }); playing.push_back (nt.note); }
                    if (p.gate < 0.999f) { offPending = true; offAtPpq = stepStart + stepLen * std::max (0.05f, p.gate); }
                }
            }
        }
        if (! hostPlaying) internalPpq += n * ppqPerSample;
        if (held.empty() && ! playing.empty() && ! offPending) releaseAll (n - 1);
        return out;
    }

    // when the arp is switched off: stop what is sounding
    const std::vector<NoteEvent>& flush()
    {
        out.clear();
        releaseAll (0);
        offPending = false;
        return out;
    }

    int heldCount() const { return (int) held.size(); }

private:
    struct Held { int note; float vel; };

    void remove (int note) { held.erase (std::remove_if (held.begin(), held.end(), [note] (auto& h) { return h.note == note; }), held.end()); }

    void releaseAll (int sample)
    {
        for (int nt : playing) out.push_back ({ sample, nt, 0.0f, false });
        playing.clear();
    }

    std::vector<Held> nextNotes (const ArpParams& p)
    {
        std::vector<Held> base = held;
        if (p.mode != 3) std::sort (base.begin(), base.end(), [] (auto& a, auto& b) { return a.note < b.note; });
        std::vector<Held> seq;
        for (int o = 0; o < std::clamp (p.octaves, 1, 4); ++o)
            for (auto h : base) { h.note += 12 * o; if (h.note <= 127) seq.push_back (h); }
        if (seq.empty()) return {};

        const int len = (int) seq.size();
        const int s = stepCounter++;
        switch (p.mode)
        {
            case 1: return { seq[(size_t) (len - 1 - s % len)] };                                   // down
            case 2: { if (len == 1) return { seq[0] };                                              // up/down
                      const int cyc = 2 * len - 2, k = s % cyc;
                      return { seq[(size_t) (k < len ? k : cyc - k)] }; }
            case 4: rng = rng * 1664525u + 1013904223u; return { seq[(size_t) ((rng >> 8) % (uint32_t) len)] }; // random
            case 5: return base;                                                                    // chord
            default: return { seq[(size_t) (s % len)] };                                            // up / as played
        }
    }

    std::vector<Held> held;
    std::vector<int> playing;
    std::vector<NoteEvent> out;
    int physicalDown = 0, stepCounter = 0;
    long long lastStep = -1;
    double internalPpq = 0.0, offAtPpq = 0.0;
    bool offPending = false;
    uint32_t rng = 0xabcdef;
};

} // namespace halcyon
