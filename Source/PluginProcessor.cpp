#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
using namespace juce;

namespace
{
    const StringArray waveNames   { "Saw", "Pulse", "Triangle", "Sine", "Noise" };
    const StringArray targetNames { "None", "Cutoff A", "Cutoff B", "Cutoff A+B", "Pitch", "Pitch A", "Pitch B", "Reso", "Pan", "Volume" };
    const StringArray lfoWaves    { "Sine", "Triangle", "Saw", "Square", "S&H" };

    NormalisableRange<float> timeRange (float maxSec) { return NormalisableRange<float> (0.0005f, maxSec, 0.0f, 0.25f); }

    String secText (float s, int) { return s < 1.0f ? String (roundToInt (s * 1000.0f)) + " ms" : String (s, 2) + " s"; }
    String pctText (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; }
    String hzText  (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + "k" : String (roundToInt (v)); }

    struct Preset { const char* name; std::vector<std::pair<String, float>> values; };

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> list = {
            { "Init", {} },
            { "Lead - Supersaw", {
                { "a1_voices", 8 }, { "a1_detune", 0.35f }, { "a1_vol", 0.8f },
                { "a2_on", 1 }, { "a2_oct", 1 }, { "a2_voices", 6 }, { "a2_detune", 0.3f }, { "a2_vol", 0.5f },
                { "b1_on", 1 }, { "b1_oct", -1 }, { "b1_voices", 4 }, { "b1_detune", 0.25f }, { "b1_vol", 0.45f },
                { "a_cutoff", 9000 }, { "a_reso", 0.1f }, { "b_cutoff", 6000 },
                { "a_d", 0.5f }, { "a_s", 0.85f }, { "a_r", 0.35f }, { "b_d", 0.5f }, { "b_s", 0.85f }, { "b_r", 0.35f },
                { "master", -10 } } },
            { "Pluck - Trance", {
                { "a1_voices", 6 }, { "a1_detune", 0.25f },
                { "a2_on", 1 }, { "a2_wave", 1 }, { "a2_oct", 1 }, { "a2_voices", 2 }, { "a2_detune", 0.1f }, { "a2_vol", 0.4f },
                { "a_cutoff", 300 }, { "a_reso", 0.25f }, { "a_key", 0.5f },
                { "m1_a", 0.001f }, { "m1_d", 0.22f }, { "m1_s", 0 }, { "m1_r", 0.2f }, { "m1_amt", 0.55f }, { "m1_target", 1 },
                { "a_d", 0.45f }, { "a_s", 0 }, { "a_r", 0.3f }, { "master", -8 } } },
            { "Pad - Warm", {
                { "a1_voices", 8 }, { "a1_detune", 0.25f },
                { "a2_on", 1 }, { "a2_wave", 1 }, { "a2_oct", -1 }, { "a2_voices", 4 }, { "a2_detune", 0.2f }, { "a2_vol", 0.5f },
                { "b1_on", 1 }, { "b1_wave", 2 }, { "b1_vol", 0.5f },
                { "a_cutoff", 1800 }, { "a_reso", 0.2f }, { "b_cutoff", 2500 }, { "b_slope", 0 },
                { "a_a", 1.2f }, { "a_d", 1.0f }, { "a_s", 0.9f }, { "a_r", 2.5f }, { "b_a", 1.2f }, { "b_d", 1.0f }, { "b_s", 0.9f }, { "b_r", 2.5f },
                { "l1_wave", 0 }, { "l1_rate", 0.15f }, { "l1_amt", 0.12f }, { "l1_target", 3 }, { "master", -12 } } },
            { "Bass - Reese", {
                { "mode", 1 }, { "porta", 0.05f },
                { "a1_oct", -2 }, { "a1_voices", 2 }, { "a1_detune", 0.12f },
                { "a2_on", 1 }, { "a2_oct", -2 }, { "a2_fine", 8 }, { "a2_voices", 2 }, { "a2_detune", 0.1f },
                { "b1_on", 1 }, { "b1_wave", 3 }, { "b1_oct", -3 }, { "b1_vol", 0.6f },
                { "a_cutoff", 700 }, { "a_reso", 0.15f }, { "a_drive", 0.4f },
                { "a_s", 1 }, { "a_r", 0.15f }, { "b_s", 1 }, { "b_r", 0.15f },
                { "l1_wave", 1 }, { "l1_rate", 0.25f }, { "l1_amt", 0.08f }, { "l1_target", 1 }, { "master", -8 } } },
            { "Stab - Melodic", {
                { "a1_voices", 4 }, { "a1_detune", 0.2f },
                { "a2_on", 1 }, { "a2_semi", 7 }, { "a2_voices", 4 }, { "a2_detune", 0.2f }, { "a2_vol", 0.6f },
                { "b1_on", 1 }, { "b1_wave", 1 }, { "b1_oct", 1 }, { "b1_vol", 0.3f },
                { "a_cutoff", 900 }, { "a_reso", 0.35f }, { "b_cutoff", 1500 },
                { "m1_d", 0.35f }, { "m1_s", 0.1f }, { "m1_amt", 0.45f }, { "m1_target", 3 },
                { "a_d", 0.6f }, { "a_s", 0.2f }, { "a_r", 0.4f }, { "b_d", 0.6f }, { "b_s", 0.2f }, { "b_r", 0.4f },
                { "master", -10 } } },
        };
        return list;
    }
}

//==============================================================================
APVTS::ParameterLayout HalcyonProcessor::createLayout()
{
    APVTS::ParameterLayout l;
    auto addF = [&] (const String& id, const String& name, NormalisableRange<float> r, float def,
                     std::function<String (float, int)> txt = nullptr, const String& label = {})
    {
        auto attr = AudioParameterFloatAttributes().withLabel (label);
        if (txt) attr = attr.withStringFromValueFunction (txt);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, attr));
    };
    auto addI = [&] (const String& id, const String& name, int lo, int hi, int def) { l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };
    auto addC = [&] (const String& id, const String& name, const StringArray& c, int def) { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, c, def)); };
    auto addB = [&] (const String& id, const String& name, bool def) { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    for (int o = 0; o < 4; ++o)
    {
        const String pre = ids::osc[o], nm = "Osc " + String (ids::osc[o]).toUpperCase() + " ";
        addB (pre + "_on", nm + "On", o == 0);
        addC (pre + "_wave", nm + "Wave", waveNames, 0);
        addI (pre + "_oct", nm + "Octave", -3, 3, 0);
        addI (pre + "_semi", nm + "Semi", -12, 12, 0);
        addF (pre + "_fine", nm + "Fine", { -100.0f, 100.0f, 0.1f }, 0.0f, nullptr, "ct");
        addF (pre + "_vol", nm + "Volume", { 0.0f, 1.0f }, 0.7f, pctText);
        addF (pre + "_pan", nm + "Pan", { -1.0f, 1.0f }, 0.0f, pctText);
        addI (pre + "_voices", nm + "Voices", 1, 8, 1);
        addF (pre + "_detune", nm + "Detune", { 0.0f, 1.0f }, 0.2f, pctText);
        addB (pre + "_retrig", nm + "Retrig", false);
    }
    for (int p = 0; p < 2; ++p)
    {
        const String pre = ids::part[p], nm = "Filter " + pre.toUpperCase() + " ";
        addC (pre + "_type", nm + "Type", { "Lowpass", "Bandpass", "Highpass" }, 0);
        addC (pre + "_slope", nm + "Slope", { "12 dB", "24 dB" }, 1);
        addF (pre + "_cutoff", nm + "Cutoff", { 20.0f, 20000.0f, 0.0f, 0.25f }, 20000.0f, hzText, "Hz");
        addF (pre + "_reso", nm + "Resonance", { 0.0f, 1.0f }, 0.0f, pctText);
        addF (pre + "_drive", nm + "Drive", { 0.0f, 1.0f }, 0.0f, pctText);
        addF (pre + "_key", nm + "Key Track", { 0.0f, 1.0f }, 0.0f, pctText);

        const String an = "Amp Env " + pre.toUpperCase() + " ";
        addF (pre + "_a", an + "Attack", timeRange (10.0f), 0.002f, secText);
        addF (pre + "_d", an + "Decay", timeRange (10.0f), 0.3f, secText);
        addF (pre + "_s", an + "Sustain", { 0.0f, 1.0f }, 1.0f, pctText);
        addF (pre + "_r", an + "Release", timeRange (15.0f), 0.2f, secText);
    }
    for (int m = 1; m <= 2; ++m)
    {
        const String pre = "m" + String (m), nm = "Mod Env " + String (m) + " ";
        addF (pre + "_a", nm + "Attack", timeRange (10.0f), 0.001f, secText);
        addF (pre + "_d", nm + "Decay", timeRange (10.0f), 0.3f, secText);
        addF (pre + "_s", nm + "Sustain", { 0.0f, 1.0f }, 0.0f, pctText);
        addF (pre + "_r", nm + "Release", timeRange (15.0f), 0.3f, secText);
        addF (pre + "_amt", nm + "Amount", { -1.0f, 1.0f }, 0.0f, pctText);
        addC (pre + "_target", nm + "Target", targetNames, 0);

        const String lp = "l" + String (m), ln = "LFO " + String (m) + " ";
        addC (lp + "_wave", ln + "Wave", lfoWaves, 0);
        addF (lp + "_rate", ln + "Rate", { 0.01f, 30.0f, 0.0f, 0.3f }, 1.0f, [] (float v, int) { return String (v, 2) + " Hz"; });
        addF (lp + "_amt", ln + "Amount", { -1.0f, 1.0f }, 0.0f, pctText);
        addC (lp + "_target", ln + "Target", targetNames, 0);
    }
    addC ("mode", "Voice Mode", { "Poly", "Mono", "Legato" }, 0);
    addI ("poly", "Polyphony", 1, 16, 8);
    addF ("porta", "Portamento", { 0.0f, 1.0f, 0.0f, 0.5f }, 0.0f, secText);
    addF ("bend", "Bend Range", { 0.0f, 24.0f, 1.0f }, 2.0f, nullptr, "st");
    addF ("master", "Master", { -36.0f, 6.0f, 0.1f }, -6.0f, nullptr, "dB");
    return l;
}

//==============================================================================
HalcyonProcessor::HalcyonProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HalcyonState", createLayout())
{
}

halcyon::Params HalcyonProcessor::readParams() const
{
    halcyon::Params p;
    for (int o = 0; o < 4; ++o)
    {
        const String pre = ids::osc[o];
        auto& op = p.osc[o];
        op.on = v (pre + "_on") > 0.5f;           op.wave = (int) v (pre + "_wave");
        op.oct = (int) v (pre + "_oct");          op.semi = (int) v (pre + "_semi");
        op.fine = v (pre + "_fine");              op.vol = v (pre + "_vol");
        op.pan = v (pre + "_pan");                op.voices = (int) v (pre + "_voices");
        op.detune = v (pre + "_detune");          op.retrig = v (pre + "_retrig") > 0.5f;
    }
    for (int i = 0; i < 2; ++i)
    {
        const String pre = ids::part[i];
        auto& f = p.filt[i];
        f.type = (int) v (pre + "_type"); f.slope = (int) v (pre + "_slope"); f.cutoff = v (pre + "_cutoff");
        f.reso = v (pre + "_reso");       f.drive = v (pre + "_drive");       f.keytrack = v (pre + "_key");
        p.amp[i] = { v (pre + "_a"), v (pre + "_d"), v (pre + "_s"), v (pre + "_r") };

        const String m = "m" + String (i + 1), lf = "l" + String (i + 1);
        p.menv[i].env = { v (m + "_a"), v (m + "_d"), v (m + "_s"), v (m + "_r") };
        p.menv[i].amount = v (m + "_amt");  p.menv[i].target = (int) v (m + "_target");
        p.lfo[i].wave = (int) v (lf + "_wave"); p.lfo[i].rate = v (lf + "_rate");
        p.lfo[i].amount = v (lf + "_amt");      p.lfo[i].target = (int) v (lf + "_target");
    }
    p.mode = (int) v ("mode");  p.polyphony = (int) v ("poly");  p.porta = v ("porta");
    p.bendRange = v ("bend");   p.masterDb = v ("master");
    return p;
}

void HalcyonProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keyboardState.reset();
}

bool HalcyonProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void HalcyonProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    engine.setParams (readParams());

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = jlimit (0, buffer.getNumSamples(), meta.samplePosition);
        if (at > pos) { engine.render (L + pos, R + pos, at - pos); pos = at; }
        const auto m = meta.getMessage();
        if (m.isNoteOn())                               engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())                         engine.noteOff (m.getNoteNumber());
        else if (m.isPitchWheel())                      engine.pitchBend ((m.getPitchWheelValue() - 8192) / 8192.0f);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }
    if (pos < buffer.getNumSamples()) engine.render (L + pos, R + pos, buffer.getNumSamples() - pos);
    activeVoices = engine.activeVoiceCount();
}

//==============================================================================
int HalcyonProcessor::getNumPrograms() { return (int) presets().size(); }
const String HalcyonProcessor::getProgramName (int i) { return isPositiveAndBelow (i, getNumPrograms()) ? presets()[(size_t) i].name : ""; }

void HalcyonProcessor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, getNumPrograms())) return;
    currentPreset = index;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm)) rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, value] : presets()[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id)) rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}

void HalcyonProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void HalcyonProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = ValueTree::fromXml (*xml);
            currentPreset = (int) state.getProperty ("preset", 0);
            apvts.replaceState (state);
        }
}

AudioProcessorEditor* HalcyonProcessor::createEditor() { return new HalcyonEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HalcyonProcessor(); }
