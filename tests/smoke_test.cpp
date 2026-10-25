// Headless smoke test: drives the real processor + editor, renders a demo of every preset.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    HalcyonProcessor proc;
    const double sr = 48000.0; const int block = 256;
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);

    juce::AudioBuffer<float> buf (2, block), demo (2, (int) (sr * 60)); demo.clear();
    juce::MidiBuffer midi;
    int demoPos = 0; bool bad = false;

    // Am - F - C - G at 128 BPM, one bar each; bass presets play roots, others play chords
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
    const double beat = 60.0 / 128.0;
    for (int pr = 0; pr < proc.getNumPrograms(); ++pr)
    {
        proc.setCurrentProgram (pr);
        proc.prepareToPlay (sr, block);
        const bool bass = proc.getProgramName (pr).startsWith ("Bass");
        const bool pluck = proc.getProgramName (pr).startsWith ("Pluck") || proc.getProgramName (pr).startsWith ("Stab");
        struct Ev { int sample; int note; bool on; };
        std::vector<Ev> evs;
        for (int c = 0; c < 4; ++c)
        {
            const double barStart = c * 4 * beat;
            if (bass)
                for (int e = 0; e < 8; ++e) // offbeat 8ths
                {
                    const double t = barStart + e * beat * 0.5;
                    evs.push_back ({ (int) (t * sr), chords[c][0] - 24, true });
                    evs.push_back ({ (int) ((t + beat * 0.4) * sr), chords[c][0] - 24, false });
                }
            else if (pluck)
                for (int e = 0; e < 8; ++e)
                {
                    const double t = barStart + e * beat * 0.5 + (e % 2) * 0.0;
                    for (int n : chords[c]) { evs.push_back ({ (int) (t * sr), n + 12 * (e % 3 == 2), true }); evs.push_back ({ (int) ((t + beat * 0.3) * sr), n + 12 * (e % 3 == 2), false }); }
                }
            else
                for (int n : chords[c]) { evs.push_back ({ (int) (barStart * sr), n + (pr == 1 ? 12 : 0), true }); evs.push_back ({ (int) ((barStart + 4 * beat - 0.05) * sr), n + (pr == 1 ? 12 : 0), false }); }
        }
        const int total = (int) ((16 * beat + 1.5) * sr);
        float peak = 0; int maxVoices = 0;
        for (int pos = 0; pos < total; pos += block)
        {
            midi.clear();
            for (auto& e : evs)
                if (e.sample >= pos && e.sample < pos + block)
                    midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, 0.9f) : juce::MidiMessage::noteOff (1, e.note), e.sample - pos);
            proc.processBlock (buf, midi);
            maxVoices = juce::jmax (maxVoices, proc.activeVoices.load());
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float x = buf.getSample (ch, i);
                    if (! std::isfinite (x)) bad = true;
                    peak = juce::jmax (peak, std::abs (x));
                }
            if (demoPos + pos + block <= demo.getNumSamples())
                for (int ch = 0; ch < 2; ++ch) demo.copyFrom (ch, demoPos + pos, buf, ch, 0, block);
        }
        // stereo width: correlation of L and R over this preset
        double lr = 0, ll = 0, rr = 0;
        for (int i = demoPos; i < demoPos + total && i < demo.getNumSamples(); ++i)
        { const double a = demo.getSample (0, i), b = demo.getSample (1, i); lr += a * b; ll += a * a; rr += b * b; }
        std::printf ("%-16s peak %.2f  max voices %2d  L/R correlation %.2f  remaining voices %d\n",
                     proc.getProgramName (pr).toRawUTF8(), peak, maxVoices, lr / std::sqrt (ll * rr + 1e-12), proc.activeVoices.load());
        demoPos += total;
    }
    const float g = 0.89f / juce::jmax (0.001f, demo.getMagnitude (0, demoPos));
    demo.applyGain (g);
    juce::WavAudioFormat wav;
    auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("halcyon_demo.wav"); f.deleteFile();
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 16, {}, 0)))
        w->writeFromAudioSampleBuffer (demo, 0, demoPos);

    proc.setCurrentProgram (1);
    juce::MemoryBlock state; proc.getStateInformation (state);
    proc.setCurrentProgram (0);
    proc.setStateInformation (state.getData(), (int) state.getSize());
    std::printf ("STATE: a1 voices restored to %.0f (expect 8)\n", proc.apvts.getRawParameterValue ("a1_voices")->load());

    proc.setCurrentProgram (2);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    auto pf = juce::File::getCurrentWorkingDirectory().getChildFile ("halcyon_ui.png"); pf.deleteFile();
    juce::FileOutputStream os (pf); juce::PNGImageFormat().writeImageToStream (img, os);
    if (auto* he = dynamic_cast<HalcyonEditor*> (ed.get()))
    {
        he->showPage (true);
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        auto pf2 = juce::File::getCurrentWorkingDirectory().getChildFile ("halcyon_fx.png"); pf2.deleteFile();
        juce::FileOutputStream os2 (pf2); juce::PNGImageFormat().writeImageToStream (img2, os2);
    }
    std::printf ("UI: %dx%d\n", ed->getWidth(), ed->getHeight());
    ed.reset();
    return bad ? 1 : 0;
}
