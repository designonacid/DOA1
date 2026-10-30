#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
using namespace juce;

namespace
{
    const StringArray waveNames   { "Saw", "Pulse", "Triangle", "Sine", "Noise" };
    const StringArray targetNames { "None", "Cutoff A", "Cutoff B", "Cutoff A+B", "Pitch", "Pitch A", "Pitch B", "Reso", "Pan", "Volume",
                                    "Detune", "Drive", "Balance A/B" };
    const StringArray sourceNames { "None", "Velocity", "Mod Wheel", "Aftertouch", "Key", "LFO 1", "LFO 2", "Mod Env 1", "Mod Env 2", "Random", "LFO 1 x Wheel" };
    const StringArray lfoWaves    { "Sine", "Triangle", "Saw", "Square", "S&H" };

    NormalisableRange<float> timeRange (float maxSec) { return NormalisableRange<float> (0.0005f, maxSec, 0.0f, 0.25f); }

    String secText (float s, int) { return s < 1.0f ? String (roundToInt (s * 1000.0f)) + " ms" : String (s, 2) + " s"; }
    String pctText (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; }
    String hzText  (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + "k" : String (roundToInt (v)); }

    struct Preset { const char* name; std::vector<std::pair<String, float>> values; };

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> list = {
            #include "FactoryPresets.inc"
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

    // ---- arpeggiator ----
    StringArray arpRates; for (auto* nm : halcyon::kArpRateNames) arpRates.add (nm);
    addB ("arp_on", "Arp On", false);
    addC ("arp_mode", "Arp Mode", { "Up", "Down", "Up/Down", "As Played", "Random", "Chord" }, 0);
    addC ("arp_rate", "Arp Rate", arpRates, 3);
    addI ("arp_oct", "Arp Octaves", 1, 4, 1);
    addF ("arp_gate", "Arp Gate", { 0.05f, 1.0f }, 0.5f, pctText);
    addF ("arp_swing", "Arp Swing", { 0.0f, 0.5f }, 0.0f, pctText);
    addB ("arp_hold", "Arp Hold", false);

    // ---- mod matrix ----
    for (int m = 1; m <= halcyon::kNumSlots; ++m)
    {
        const String pre = "mm" + String (m), nm = "Matrix " + String (m) + " ";
        addC (pre + "_src", nm + "Source", sourceNames, 0);
        addC (pre + "_dst", nm + "Destination", targetNames, 0);
        addF (pre + "_amt", nm + "Amount", { -1.0f, 1.0f }, 0.0f, pctText);
    }

    // ---- effects ----
    addB ("dist_on", "Distortion On", false);
    addC ("dist_type", "Distortion Type", { "Soft", "Hard", "Fold", "Crush" }, 0);
    addF ("dist_amt", "Distortion Amount", { 0.0f, 1.0f }, 0.3f, pctText);
    addF ("dist_mix", "Distortion Mix", { 0.0f, 1.0f }, 1.0f, pctText);

    addB ("ph_on", "Phaser On", false);
    addF ("ph_rate", "Phaser Rate", { 0.02f, 10.0f, 0.0f, 0.35f }, 0.3f, [] (float v, int) { return String (v, 2) + " Hz"; });
    addF ("ph_depth", "Phaser Depth", { 0.0f, 1.0f }, 0.6f, pctText);
    addF ("ph_fb", "Phaser Feedback", { 0.0f, 0.9f }, 0.4f, pctText);
    addF ("ph_mix", "Phaser Mix", { 0.0f, 1.0f }, 0.5f, pctText);

    addB ("ch_on", "Chorus On", false);
    addF ("ch_rate", "Chorus Rate", { 0.02f, 5.0f, 0.0f, 0.4f }, 0.6f, [] (float v, int) { return String (v, 2) + " Hz"; });
    addF ("ch_depth", "Chorus Depth", { 0.0f, 1.0f }, 0.4f, pctText);
    addF ("ch_delay", "Chorus Delay", { 5.0f, 30.0f, 0.1f }, 12.0f, [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; });
    addF ("ch_mix", "Chorus Mix", { 0.0f, 1.0f }, 0.4f, pctText);

    addB ("eq_on", "EQ On", false);
    addF ("eq_low", "EQ Bass", { -15.0f, 15.0f, 0.1f }, 0.0f, nullptr, "dB");
    addF ("eq_high", "EQ Treble", { -15.0f, 15.0f, 0.1f }, 0.0f, nullptr, "dB");

    StringArray delayTimes; for (auto* nm : halcyon::kDelayNames) delayTimes.add (nm);
    addB ("dl_on", "Delay On", false);
    addC ("dl_time", "Delay Time", delayTimes, 3);
    addF ("dl_fb", "Delay Feedback", { 0.0f, 0.95f }, 0.35f, pctText);
    addB ("dl_pp", "Delay Ping-Pong", true);
    addF ("dl_tone", "Delay Tone", { 500.0f, 20000.0f, 0.0f, 0.3f }, 6000.0f, hzText, "Hz");
    addF ("dl_mix", "Delay Mix", { 0.0f, 1.0f }, 0.25f, pctText);

    addB ("rv_on", "Reverb On", false);
    addF ("rv_size", "Reverb Size", { 0.0f, 1.0f }, 0.7f, pctText);
    addF ("rv_damp", "Reverb Damping", { 0.0f, 1.0f }, 0.5f, pctText);
    addF ("rv_width", "Reverb Width", { 0.0f, 1.0f }, 1.0f, pctText);
    addF ("rv_mix", "Reverb Mix", { 0.0f, 1.0f }, 0.25f, pctText);

    addB ("cp_on", "Compressor On", false);
    addF ("cp_thresh", "Comp Threshold", { -40.0f, 0.0f, 0.1f }, -12.0f, nullptr, "dB");
    addF ("cp_ratio", "Comp Ratio", { 1.0f, 20.0f, 0.1f, 0.5f }, 4.0f, [] (float v, int) { return String (v, 1) + ":1"; });
    addF ("cp_att", "Comp Attack", { 0.1f, 100.0f, 0.0f, 0.4f }, 5.0f, [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; });
    addF ("cp_rel", "Comp Release", { 10.0f, 1000.0f, 0.0f, 0.4f }, 120.0f, [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; });
    addF ("cp_makeup", "Comp Makeup", { 0.0f, 24.0f, 0.1f }, 0.0f, nullptr, "dB");
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
    for (int m = 0; m < halcyon::kNumSlots; ++m)
    {
        const String pre = "mm" + String (m + 1);
        p.matrix[m] = { (int) v (pre + "_src"), (int) v (pre + "_dst"), v (pre + "_amt") };
    }
    p.mode = (int) v ("mode");  p.polyphony = (int) v ("poly");  p.porta = v ("porta");
    p.bendRange = v ("bend");   p.masterDb = v ("master");
    return p;
}

halcyon::FxParams HalcyonProcessor::readFxParams() const
{
    halcyon::FxParams f;
    f.distOn = v ("dist_on") > 0.5f; f.distType = (int) v ("dist_type"); f.distAmount = v ("dist_amt"); f.distMix = v ("dist_mix");
    f.phOn = v ("ph_on") > 0.5f; f.phRate = v ("ph_rate"); f.phDepth = v ("ph_depth"); f.phFeedback = v ("ph_fb"); f.phMix = v ("ph_mix");
    f.chOn = v ("ch_on") > 0.5f; f.chRate = v ("ch_rate"); f.chDepth = v ("ch_depth"); f.chDelayMs = v ("ch_delay"); f.chMix = v ("ch_mix");
    f.eqOn = v ("eq_on") > 0.5f; f.eqLowDb = v ("eq_low"); f.eqHighDb = v ("eq_high");
    f.dlOn = v ("dl_on") > 0.5f; f.dlTime = (int) v ("dl_time"); f.dlFeedback = v ("dl_fb"); f.dlPingPong = v ("dl_pp") > 0.5f;
    f.dlToneHz = v ("dl_tone"); f.dlMix = v ("dl_mix");
    f.rvOn = v ("rv_on") > 0.5f; f.rvSize = v ("rv_size"); f.rvDamp = v ("rv_damp"); f.rvWidth = v ("rv_width"); f.rvMix = v ("rv_mix");
    f.cpOn = v ("cp_on") > 0.5f; f.cpThreshDb = v ("cp_thresh"); f.cpRatio = v ("cp_ratio"); f.cpAttackMs = v ("cp_att");
    f.cpReleaseMs = v ("cp_rel"); f.cpMakeupDb = v ("cp_makeup");
    return f;
}

halcyon::ArpParams HalcyonProcessor::readArpParams() const
{
    halcyon::ArpParams a;
    a.on = v ("arp_on") > 0.5f; a.mode = (int) v ("arp_mode"); a.rate = (int) v ("arp_rate"); a.octaves = (int) v ("arp_oct");
    a.gate = v ("arp_gate"); a.swing = v ("arp_swing"); a.hold = v ("arp_hold") > 0.5f;
    return a;
}

void HalcyonProcessor::handleEngineMessage (const MidiMessage& m)
{
    if (m.isNoteOn())                                engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())                          engine.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())                       engine.pitchBend ((m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isController() && m.getControllerNumber() == 1) engine.modWheel (m.getControllerValue() / 127.0f);
    else if (m.isChannelPressure())                  engine.aftertouch (m.getChannelPressureValue() / 127.0f);
    else if (m.isAftertouch())                       engine.aftertouch (m.getAfterTouchValue() / 127.0f);
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
}

void HalcyonProcessor::prepareToPlay (double sampleRate, int)
{
    arp.reset();
    arpWasOn = false;
    engine.prepare (sampleRate);
    fxChain.prepare (sampleRate);
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
    const int numSamples = buffer.getNumSamples();

    double bpm = 128.0, hostPpq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto position = ph->getPosition())
        {
            if (auto hostBpm = position->getBpm()) bpm = *hostBpm;
            if (position->getIsPlaying())
                if (auto pq = position->getPpqPosition()) { hostPpq = *pq; hostPlaying = true; }
        }

    // ---- arpeggiator: turns held notes into a stream of note events ----
    const auto ap = readArpParams();
    MidiBuffer arpMidi;
    if (ap.on || arpWasOn)
    {
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())       arp.noteOn (m.getNoteNumber(), m.getFloatVelocity(), ap.hold);
            else if (m.isNoteOff()) arp.noteOff (m.getNoteNumber(), ap.hold);
            else                    arpMidi.addEvent (m, meta.samplePosition);   // pass through bend, wheel...
        }
        if (arpHoldWasOn && ! ap.hold) arp.releaseLatch();
        const auto& events = ap.on ? arp.process (numSamples, getSampleRate(), bpm, hostPlaying, hostPpq, ap) : arp.flush();
        for (auto& e : events)
            arpMidi.addEvent (e.on ? MidiMessage::noteOn (1, e.note, jlimit (0.01f, 1.0f, e.velocity))
                                   : MidiMessage::noteOff (1, e.note), e.sample);
        if (! ap.on) arp.reset();
    }
    arpWasOn = ap.on;
    arpHoldWasOn = ap.hold;
    const MidiBuffer& events = (ap.on || ! arpMidi.isEmpty()) ? arpMidi : midi;

    int pos = 0;
    for (const auto meta : events)
    {
        const int at = jlimit (0, numSamples, meta.samplePosition);
        if (at > pos) { engine.render (L + pos, R + pos, at - pos); pos = at; }
        handleEngineMessage (meta.getMessage());
    }
    if (pos < numSamples) engine.render (L + pos, R + pos, numSamples - pos);
    activeVoices = engine.activeVoiceCount();
    fxChain.process (L, R, buffer.getNumSamples(), readFxParams(), bpm);
    buffer.applyGain (Decibels::decibelsToGain (v ("master"), -100.0f));   // master is post-FX
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

bool HalcyonProcessor::loadPresetFile (const File& file, String& nameOut)
{
    const auto doc = JSON::parse (file.loadFileAsString());
    if (! doc.isObject() || doc["format"].toString() != "DOA1 preset") return false;
    auto* params = doc["params"].getDynamicObject();
    if (params == nullptr) return false;
    for (auto* prm : getParameters())                      // start from defaults, like factory presets
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm)) rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& prop : params->getProperties())
        if (auto* rp = apvts.getParameter (prop.name.toString()))
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) (double) prop.value));
    nameOut = doc["name"].toString().isNotEmpty() ? doc["name"].toString() : file.getFileNameWithoutExtension();
    userPresetName = nameOut;
    return true;
}

bool HalcyonProcessor::savePresetFile (const File& file, const String& name)
{
    auto params = std::make_unique<DynamicObject>();
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
            params->setProperty (rp->paramID, rp->convertFrom0to1 (rp->getValue()));
    auto doc = std::make_unique<DynamicObject>();
    doc->setProperty ("format", "DOA1 preset");
    doc->setProperty ("version", 1);
    doc->setProperty ("name", name);
    doc->setProperty ("params", var (params.release()));
    return file.replaceWithText (JSON::toString (var (doc.release())));
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
