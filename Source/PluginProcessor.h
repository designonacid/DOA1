#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "DSP/SynthEngine.h"

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

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // on-screen keyboard
    std::atomic<int> activeVoices { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    halcyon::Params readParams() const;
    float v (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    halcyon::Engine engine;
    int currentPreset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HalcyonProcessor)
};
