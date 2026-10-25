#pragma once

#include "PluginProcessor.h"

namespace hui
{
    const juce::Colour bg      { 0xff15171c };
    const juce::Colour panel   { 0xff1f2229 };
    const juce::Colour panelHi { 0xff2b2f38 };
    const juce::Colour text    { 0xffdde1e8 };
    const juce::Colour dim     { 0xff8a90a0 };
    const juce::Colour accent  { 0xffffb347 };   // Halcyon amber
    const juce::Colour partB   { 0xff5ec8ff };   // part B tint

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (12.5f, juce::Font::bold); }
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    };

    // A labelled control bound to a parameter: knob, choice box or toggle
    class Control : public juce::Component
    {
    public:
        enum Kind { KnobKind, ChoiceKind, ToggleKind };
        Control (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label, Kind, juce::Colour tint);
        void resized() override;
        const Kind kind;
    private:
        juce::Label label;
        juce::Slider slider;
        juce::ComboBox box;
        juce::TextButton toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bA;
    };

    class Panel : public juce::Component
    {
    public:
        Panel (const juce::String& t, juce::Colour c) : title (t), tint (c) {}
        void add (Control* c, float weight = 1.0f) { addAndMakeVisible (c); items.push_back ({ std::unique_ptr<Control> (c), weight }); }
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        juce::String title;
        juce::Colour tint;
        struct Item { std::unique_ptr<Control> c; float weight; };
        std::vector<Item> items;
    };
}

//==============================================================================
class HalcyonEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HalcyonEditor (HalcyonProcessor&);
    ~HalcyonEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    HalcyonProcessor& proc;
    hui::LookAndFeel lnf;
    juce::OwnedArray<hui::Panel> panels;   // see resized() for order
    juce::ComboBox presetBox;
    juce::Label voiceLabel;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HalcyonEditor)
};
