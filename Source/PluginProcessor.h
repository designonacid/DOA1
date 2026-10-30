#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "DSP/SynthEngine.h"
#include "DSP/Effects.h"
#include "DSP/Arpeggiator.h"

namespace ids
{
    inline const char* osc[4]  = { "a1", "a2", "b1", "b2" };
    inline const char* part[2] = { "a", "b" };
    inline juce::String p (const juce::String& prefix, const char* field) { return prefix + "_" + field; }
}

class HalcyonProcessor : public juce::AudioProcessor
{
public:
    HalcyonProcessor();
    ~HalcyonProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // User preset files (.doa1, JSON): written by SAVE and by the DOA1 MCP sound matcher
    bool loadPresetFile (const juce::File& file, juce::String& nameOut);
    bool savePresetFile (const juce::File& file, const juce::String& name);
    juce::String userPresetName;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // on-screen keyboard
    std::atomic<int> activeVoices { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    halcyon::Params readParams() const;
    halcyon::FxParams readFxParams() const;
    halcyon::ArpParams readArpParams() const;
    void handleEngineMessage (const juce::MidiMessage&);
    float v (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    halcyon::Engine engine;
    halcyon::FxChain fxChain;
    halcyon::Arpeggiator arp;
    bool arpWasOn = false, arpHoldWasOn = false;
    int currentPreset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HalcyonProcessor)
};
