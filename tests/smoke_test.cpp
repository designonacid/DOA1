// Headless smoke test: drives the real processor + editor.
// Renders every factory preset with a part that suits its category, reports level and stereo
// stats (used for level matching), writes a demo of a few presets per category, and snapshots the UI.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <map>

struct Ev { int sample; int note; bool on; };

static std::vector<Ev> partFor (const juce::String& name, double sr)
{
    const double beat = 60.0 / 128.0;
    const int chords[2][3] = { { 57, 60, 64 }, { 53, 57, 60 } };   // Am, F
    std::vector<Ev> ev;
    auto note = [&] (double t, double len, int n) { ev.push_back ({ (int) (t * sr), n, true }); ev.push_back ({ (int) ((t + len) * sr), n, false }); };
    const auto cat = name.upToFirstOccurrenceOf (" - ", false, false);
    for (int bar = 0; bar < 2; ++bar)
    {
        const double t0 = bar * 4 * beat;
        if (cat == "Lead")
        {
            const int mel[2][4] = { { 69, 72, 76, 74 }, { 72, 69, 67, 69 } };
            for (int b = 0; b < 4; ++b) note (t0 + b * beat, beat * 0.9, mel[bar][b]);
        }
        else if (cat == "Bass")
            for (int e = 0; e < 4; ++e) note (t0 + (e + 0.5) * beat, beat * 0.4, chords[bar][0] - 24);
        else if (cat == "Pluck" || cat == "Keys")
            for (int e = 0; e < 8; ++e) for (int n : chords[bar]) note (t0 + e * beat * 0.5, beat * 0.4, n);
        else if (cat == "Stab")
            for (int e = 0; e < 4; ++e) for (int n : chords[bar]) note (t0 + e * beat, beat * 0.3, n);
        else if (cat == "FX")
        { if (bar == 0) note (t0, 8 * beat - 0.05, 60); }
        else
            for (int n : chords[bar]) note (t0, 4 * beat - 0.05, n);   // pads, arps, init: held chords
    }
    return ev;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    HalcyonProcessor proc;
    const double sr = 48000.0; const int block = 256;
    proc.setPlayConfigDetails (0, 2, sr, block);

    juce::AudioBuffer<float> buf (2, block), demo (2, (int) (sr * 120)); demo.clear();
    juce::MidiBuffer midi;
    int demoPos = 0; bool bad = false;
    std::map<juce::String, int> perCategory;
    FILE* lv = std::fopen ("levels_measured.json", "w");
    std::fprintf (lv, "{\n");

    for (int pr = 0; pr < proc.getNumPrograms(); ++pr)
    {
        proc.setCurrentProgram (pr);
        proc.prepareToPlay (sr, block);
        const auto name = proc.getProgramName (pr);
        const auto evs = partFor (name, sr);
        const int total = (int) ((8 * 60.0 / 128.0 + 1.0) * sr);
        juce::AudioBuffer<float> out (2, total + block); out.clear();
        for (int pos = 0; pos < total; pos += block)
        {
            midi.clear();
            for (auto& e : evs)
                if (e.sample >= pos && e.sample < pos + block)
                    midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, 0.85f) : juce::MidiMessage::noteOff (1, e.note), e.sample - pos);
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, block);
        }
        // stats: peak, loudness proxy (RMS of the louder half of 50 ms frames), stereo correlation
        float peak = 0; double lr = 0, ll = 0, rr = 0;
        std::vector<double> frames;
        const int fl = (int) (sr * 0.05);
        for (int f = 0; f + fl <= total; f += fl)
        {
            double e = 0;
            for (int i = f; i < f + fl; ++i)
            {
                const float a = out.getSample (0, i), b = out.getSample (1, i);
                if (! std::isfinite (a) || ! std::isfinite (b)) bad = true;
                peak = juce::jmax (peak, std::abs (a), std::abs (b));
                e += 0.5 * (a * a + b * b); lr += a * b; ll += a * a; rr += b * b;
            }
            frames.push_back (e / fl);
        }
        std::sort (frames.begin(), frames.end());
        double loud = 0; for (size_t i = frames.size() / 2; i < frames.size(); ++i) loud += frames[i];
        loud /= (double) (frames.size() - frames.size() / 2);
        const double rmsDb = 10.0 * std::log10 (loud + 1e-12);
        std::printf ("%-32s peak %5.2f  loudness %6.1f dB  corr %5.2f\n", name.toRawUTF8(), peak, rmsDb, lr / std::sqrt (ll * rr + 1e-12));
        std::fprintf (lv, "  \"%s\": [%.3f, %.3f, %.3f]%s\n", name.toRawUTF8(), proc.apvts.getRawParameterValue ("master")->load(),
                      rmsDb, 20.0 * std::log10 (peak + 1e-9), pr + 1 < proc.getNumPrograms() ? "," : "");

        // demo: first two presets of each category
        const auto cat = name.upToFirstOccurrenceOf (" - ", false, false);
        if (++perCategory[cat] <= 2 && demoPos + total <= demo.getNumSamples())
        {
            for (int ch = 0; ch < 2; ++ch) demo.copyFrom (ch, demoPos, out, ch, 0, total);
            demoPos += total;
        }
    }
    std::fprintf (lv, "}\n"); std::fclose (lv);

    juce::WavAudioFormat wav;
    auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("halcyon_demo.wav"); f.deleteFile();
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 16, {}, 0)))
        w->writeFromAudioSampleBuffer (demo, 0, demoPos);

    proc.setCurrentProgram (1);
    juce::MemoryBlock state; proc.getStateInformation (state);
    proc.setCurrentProgram (0);
    proc.setStateInformation (state.getData(), (int) state.getSize());
    std::printf ("STATE: a1 voices restored to %.0f (expect 8), presets %d\n", proc.apvts.getRawParameterValue ("a1_voices")->load(), proc.getNumPrograms());

    proc.setCurrentProgram (2);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto snap = [&] (const char* file)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        auto pf = juce::File::getCurrentWorkingDirectory().getChildFile (file); pf.deleteFile();
        juce::FileOutputStream os (pf); juce::PNGImageFormat().writeImageToStream (img, os);
    };
    snap ("halcyon_ui.png");
    if (auto* he = dynamic_cast<HalcyonEditor*> (ed.get())) { he->showPage (1); snap ("halcyon_fx.png"); he->showPage (2); snap ("halcyon_mod.png"); }
    std::printf ("UI: %dx%d\n", ed->getWidth(), ed->getHeight());
    ed.reset();
    return bad ? 1 : 0;
}
