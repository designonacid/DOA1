#include "PluginEditor.h"

using namespace hui;
using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, panelHi);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, dim);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonColourId, panelHi);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, dim);
    setColour (juce::TextButton::textColourOnId, bg);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float a0, float a1, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const float track = juce::jmax (3.0f, r * 0.12f);

    juce::Path bgArc, valArc;
    bgArc.addCentredArc (c.x, c.y, r - track, r - track, 0.0f, a0, a1, true);
    g.setColour (panelHi.brighter (0.1f));
    g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // bipolar params (tune) fill from the centre
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    valArc.addCentredArc (c.x, c.y, r - track, r - track, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (valArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob cap
    const float capR = r - track * 2.4f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d46), c.x, c.y - capR,
                                             juce::Colour (0xff1e2025), c.x, c.y + capR, false));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -capR + 3.0f, 3.0f, capR * 0.45f, 1.5f);
    g.setColour (text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    auto col = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);
    if (over && ! on) col = col.brighter (0.12f);
    if (down) col = col.darker (0.15f);
    if (! b.isEnabled()) col = col.withMultipliedAlpha (0.35f);
    g.setColour (col);
    g.fillRoundedRectangle (r, 4.0f);
    if (on)
    {
        g.setColour (col.withAlpha (0.35f));
        g.drawRoundedRectangle (r.expanded (1.0f), 5.0f, 2.0f);
    }
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                  .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (r, 4.0f);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (box.getLocalBounds());
    l.setJustificationType (juce::Justification::centred);
    l.setFont (getComboBoxFont (box));
}


//==============================================================================
Control::Control (APVTS& s, const juce::String& id, const juce::String& text, Kind k, juce::Colour tint) : kind (k)
{
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    addAndMakeVisible (label);

    if (k == KnobKind)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 15);
        slider.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, tint);
        auto* prm = s.getParameter (id);
        slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        addAndMakeVisible (slider);
        sA = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
    }
    else if (k == ChoiceKind)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
            box.addItemList (c->choices, 1);
        addAndMakeVisible (box);
        cA = std::make_unique<APVTS::ComboBoxAttachment> (s, id, box);
    }
    else
    {
        toggle.setButtonText (text == "On" ? "ON" : text.toUpperCase());
        toggle.setClickingTogglesState (true);
        toggle.setColour (juce::TextButton::buttonOnColourId, tint);
        addAndMakeVisible (toggle);
        bA = std::make_unique<APVTS::ButtonAttachment> (s, id, toggle);
        label.setVisible (false);
    }
}

void Control::resized()
{
    auto r = getLocalBounds();
    if (kind == ToggleKind) { toggle.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 4, 84), 26)); return; }
    label.setBounds (r.removeFromTop (16));
    if (kind == KnobKind) slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 84), juce::jmin (r.getHeight(), 92)));
    else box.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, 26).translated (0, -6));
}

void Panel::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (tint);
    g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, 3.0f, (float) getHeight()).reduced (0, 8), 1.5f);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().reduced (12, 6).removeFromTop (14), juce::Justification::centredLeft);
}

void Panel::resized()
{
    auto r = getLocalBounds().reduced (8, 0).withTrimmedTop (22).withTrimmedBottom (6);
    float total = 0; for (auto& it : items) total += it.weight;
    const float unit = r.getWidth() / juce::jmax (1.0f, total);
    float x = (float) r.getX();
    for (auto& it : items)
    {
        it.c->setBounds (juce::roundToInt (x), r.getY(), juce::roundToInt (unit * it.weight), r.getHeight());
        x += unit * it.weight;
    }
}

//==============================================================================
HalcyonEditor::HalcyonEditor (HalcyonProcessor& p)
    : AudioProcessorEditor (&p), proc (p), keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;
    using K = Control::Kind;
    auto knob = [&] (const juce::String& id, const juce::String& l, juce::Colour t) { return new Control (s, id, l, K::KnobKind, t); };
    auto choice = [&] (const juce::String& id, const juce::String& l, juce::Colour t) { return new Control (s, id, l, K::ChoiceKind, t); };
    auto toggle = [&] (const juce::String& id, const juce::String& l, juce::Colour t) { return new Control (s, id, l, K::ToggleKind, t); };

    // 0-3: oscillators A1 A2 B1 B2
    for (int o = 0; o < 4; ++o)
    {
        const juce::String pre = ids::osc[o];
        const auto tint = o < 2 ? accent : partB;
        auto* pn = panels.add (new Panel ("OSC " + pre.toUpperCase(), tint));
        pn->add (toggle (pre + "_on", "On", tint), 0.8f);
        pn->add (choice (pre + "_wave", "Wave", tint), 1.6f);
        pn->add (knob (pre + "_oct", "Oct", tint));
        pn->add (knob (pre + "_semi", "Semi", tint));
        pn->add (knob (pre + "_fine", "Fine", tint));
        pn->add (knob (pre + "_vol", "Vol", tint));
        pn->add (knob (pre + "_pan", "Pan", tint));
        pn->add (knob (pre + "_voices", "Voices", tint));
        pn->add (knob (pre + "_detune", "Detune", tint));
        pn->add (toggle (pre + "_retrig", "Retrig", tint), 0.9f);
    }
    // 4-7: filter A, amp A, filter B, amp B
    for (int i = 0; i < 2; ++i)
    {
        const juce::String pre = ids::part[i];
        const auto tint = i == 0 ? accent : partB;
        auto* f = panels.add (new Panel ("FILTER " + pre.toUpperCase(), tint));
        f->add (choice (pre + "_type", "Type", tint), 1.5f);
        f->add (choice (pre + "_slope", "Slope", tint), 1.3f);
        f->add (knob (pre + "_cutoff", "Cutoff", tint));
        f->add (knob (pre + "_reso", "Reso", tint));
        f->add (knob (pre + "_drive", "Drive", tint));
        f->add (knob (pre + "_key", "Key", tint));
        auto* a = panels.add (new Panel ("AMP ENV " + pre.toUpperCase(), tint));
        for (auto [id, l] : { std::pair { "_a", "Att" }, { "_d", "Dec" }, { "_s", "Sus" }, { "_r", "Rel" } })
            a->add (knob (pre + id, l, tint));
    }
    // 8-9: mod envelopes, 10-11: LFOs, 12: global
    const juce::Colour modTint { 0xffc58bff };
    for (int m = 1; m <= 2; ++m)
    {
        const juce::String pre = "m" + juce::String (m);
        auto* pn = panels.add (new Panel ("MOD ENV " + juce::String (m), modTint));
        for (auto [id, l] : { std::pair { "_a", "Att" }, { "_d", "Dec" }, { "_s", "Sus" }, { "_r", "Rel" }, { "_amt", "Amount" } })
            pn->add (knob (pre + id, l, modTint));
        pn->add (choice (pre + "_target", "Target", modTint), 1.9f);
    }
    for (int m = 1; m <= 2; ++m)
    {
        const juce::String pre = "l" + juce::String (m);
        auto* pn = panels.add (new Panel ("LFO " + juce::String (m), modTint));
        pn->add (choice (pre + "_wave", "Wave", modTint), 1.6f);
        pn->add (knob (pre + "_rate", "Rate", modTint));
        pn->add (knob (pre + "_amt", "Amount", modTint));
        pn->add (choice (pre + "_target", "Target", modTint), 1.9f);
    }
    auto* g = panels.add (new Panel ("GLOBAL", text));
    g->add (choice ("mode", "Mode", accent), 1.4f);
    g->add (knob ("poly", "Voices", accent));
    g->add (knob ("porta", "Glide", accent));
    g->add (knob ("bend", "Bend", accent));
    g->add (knob ("master", "Master", accent));

    for (auto* pn : panels) addAndMakeVisible (pn);

    // ---- FX page ----
    const juce::Colour fxTint { 0xff6be3a5 };
    {
        auto* d = fxPanels.add (new Panel ("DISTORTION", fxTint));
        d->add (toggle ("dist_on", "On", fxTint), 0.9f); d->add (choice ("dist_type", "Type", fxTint), 1.5f);
        d->add (knob ("dist_amt", "Amount", fxTint)); d->add (knob ("dist_mix", "Mix", fxTint));
        auto* ph = fxPanels.add (new Panel ("PHASER", fxTint));
        ph->add (toggle ("ph_on", "On", fxTint), 0.9f);
        for (auto [id, l] : { std::pair { "ph_rate", "Rate" }, { "ph_depth", "Depth" }, { "ph_fb", "Feedback" }, { "ph_mix", "Mix" } }) ph->add (knob (id, l, fxTint));
        auto* ch = fxPanels.add (new Panel ("CHORUS", fxTint));
        ch->add (toggle ("ch_on", "On", fxTint), 0.9f);
        for (auto [id, l] : { std::pair { "ch_rate", "Rate" }, { "ch_depth", "Depth" }, { "ch_delay", "Delay" }, { "ch_mix", "Mix" } }) ch->add (knob (id, l, fxTint));
        auto* e = fxPanels.add (new Panel ("EQ", fxTint));
        e->add (toggle ("eq_on", "On", fxTint), 0.9f); e->add (knob ("eq_low", "Bass", fxTint)); e->add (knob ("eq_high", "Treble", fxTint));
        auto* dl = fxPanels.add (new Panel ("DELAY", fxTint));
        dl->add (toggle ("dl_on", "On", fxTint), 0.9f); dl->add (choice ("dl_time", "Time", fxTint), 1.4f);
        dl->add (knob ("dl_fb", "Feedback", fxTint)); dl->add (toggle ("dl_pp", "Ping-Pong", fxTint), 1.3f);
        dl->add (knob ("dl_tone", "Tone", fxTint)); dl->add (knob ("dl_mix", "Mix", fxTint));
        auto* rv = fxPanels.add (new Panel ("REVERB", fxTint));
        rv->add (toggle ("rv_on", "On", fxTint), 0.9f);
        for (auto [id, l] : { std::pair { "rv_size", "Size" }, { "rv_damp", "Damp" }, { "rv_width", "Width" }, { "rv_mix", "Mix" } }) rv->add (knob (id, l, fxTint));
        auto* cp = fxPanels.add (new Panel ("COMPRESSOR", fxTint));
        cp->add (toggle ("cp_on", "On", fxTint), 0.9f);
        for (auto [id, l] : { std::pair { "cp_thresh", "Thresh" }, { "cp_ratio", "Ratio" }, { "cp_att", "Attack" }, { "cp_rel", "Release" }, { "cp_makeup", "Makeup" } })
            cp->add (knob (id, l, fxTint));
    }
    for (auto* pn : fxPanels) addChildComponent (pn);

    // ---- ARP / MOD page ----
    {
        const juce::Colour arpTint { 0xffff6fa8 };
        auto* a = modPanels.add (new Panel ("ARPEGGIATOR", arpTint));
        a->add (toggle ("arp_on", "On", arpTint), 0.9f);
        a->add (choice ("arp_mode", "Mode", arpTint), 1.6f);
        a->add (choice ("arp_rate", "Rate", arpTint), 1.2f);
        a->add (knob ("arp_oct", "Octaves", arpTint));
        a->add (knob ("arp_gate", "Gate", arpTint));
        a->add (knob ("arp_swing", "Swing", arpTint));
        a->add (toggle ("arp_hold", "Hold", arpTint), 0.9f);
        for (int m = 1; m <= halcyon::kNumSlots; ++m)
        {
            const juce::String pre = "mm" + juce::String (m);
            auto* sl = modPanels.add (new Panel ("MOD MATRIX " + juce::String (m), modTint));
            sl->add (choice (pre + "_src", "Source", modTint), 2.0f);
            sl->add (choice (pre + "_dst", "Destination", modTint), 2.0f);
            sl->add (knob (pre + "_amt", "Amount", modTint));
        }
    }
    for (auto* pn : modPanels) addChildComponent (pn);

    for (auto* b : { &synthTab, &fxTab, &modTab })
    {
        b->setRadioGroupId (7);
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    synthTab.setToggleState (true, juce::dontSendNotification);
    synthTab.onClick = [this] { if (synthTab.getToggleState()) showPage (0); };
    fxTab.onClick    = [this] { if (fxTab.getToggleState()) showPage (1); };
    modTab.onClick   = [this] { if (modTab.getToggleState()) showPage (2); };

    // presets grouped by their "Category - Name" prefix
    {
        juce::String lastCat;
        for (int i = 0; i < proc.getNumPrograms(); ++i)
        {
            const auto name = proc.getProgramName (i);
            const auto cat = name.containsIgnoreCase (" - ") ? name.upToFirstOccurrenceOf (" - ", false, false) : juce::String ("General");
            if (cat != lastCat) { presetBox.addSectionHeading (cat.toUpperCase()); lastCat = cat; }
            presetBox.addItem (name.fromFirstOccurrenceOf (" - ", false, false).isEmpty() ? name : name.fromFirstOccurrenceOf (" - ", false, false), i + 1);
        }
    }
    loadBtn.setTooltip ("Load a .doa1 preset (for example one made by the DOA1 MCP sound matcher)");
    saveBtn.setTooltip ("Save the current sound as a .doa1 preset");
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load DOA1 preset", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("DOA1 Presets"), "*.doa1");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            juce::String name;
            if (fc.getResult().existsAsFile() && proc.loadPresetFile (fc.getResult(), name))
                presetBox.setText (name, juce::dontSendNotification);
        });
    };
    saveBtn.onClick = [this]
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("DOA1 Presets");
        dir.createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Save DOA1 preset", dir.getChildFile ("My Preset.doa1"), "*.doa1");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension ("doa1");
            if (proc.savePresetFile (f, f.getFileNameWithoutExtension())) presetBox.setText (f.getFileNameWithoutExtension(), juce::dontSendNotification);
        });
    };
    addAndMakeVisible (loadBtn);
    addAndMakeVisible (saveBtn);
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    addAndMakeVisible (prevPreset);
    addAndMakeVisible (nextPreset);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) { proc.userPresetName.clear(); proc.setCurrentProgram (presetBox.getSelectedId() - 1); } };
    addAndMakeVisible (presetBox);

    voiceLabel.setJustificationType (juce::Justification::centredRight);
    voiceLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (voiceLabel);

    keyboard.setAvailableRange (36, 96);   // C2..C7
    keyboard.setOctaveForMiddleC (4);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accent.withAlpha (0.7f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accent.withAlpha (0.25f));
    addAndMakeVisible (keyboard);

    setSize (1240, 760);
    startTimerHz (10);
}

void HalcyonEditor::stepPreset (int delta)
{
    proc.userPresetName.clear();
    const int n = proc.getNumPrograms();
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + n) % n);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

void HalcyonEditor::showPage (int page)
{
    for (auto* pn : panels)    pn->setVisible (page == 0);
    for (auto* pn : fxPanels)  pn->setVisible (page == 1);
    for (auto* pn : modPanels) pn->setVisible (page == 2);
    synthTab.setToggleState (page == 0, juce::dontSendNotification);
    fxTab.setToggleState (page == 1, juce::dontSendNotification);
    modTab.setToggleState (page == 2, juce::dontSendNotification);
}

HalcyonEditor::~HalcyonEditor() { stopTimer(); setLookAndFeel (nullptr); }

void HalcyonEditor::timerCallback()
{
    voiceLabel.setText ("VOICES " + juce::String (proc.activeVoices.load()), juce::dontSendNotification);
    if (proc.userPresetName.isEmpty() && presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

void HalcyonEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto header = getLocalBounds().removeFromTop (58).reduced (22, 0);
    g.setColour (accent);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("HALCYON", header, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("POLYPHONIC SYNTH", header.withTrimmedLeft (160), juce::Justification::centredLeft);
}

void HalcyonEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto header = r.removeFromTop (44);
    nextPreset.setBounds (header.removeFromRight (30).reduced (0, 8));
    header.removeFromRight (4);
    presetBox.setBounds (header.removeFromRight (230).reduced (0, 8));
    header.removeFromRight (4);
    prevPreset.setBounds (header.removeFromRight (30).reduced (0, 8));
    header.removeFromRight (8);
    saveBtn.setBounds (header.removeFromRight (62).reduced (0, 8));
    header.removeFromRight (4);
    loadBtn.setBounds (header.removeFromRight (62).reduced (0, 8));
    voiceLabel.setBounds (header.removeFromRight (110).reduced (8, 8));
    {
        auto tabs = header.withTrimmedLeft (340).removeFromLeft (300).reduced (0, 8);
        synthTab.setBounds (tabs.removeFromLeft (90));
        fxTab.setBounds (tabs.removeFromLeft (80));
        modTab.setBounds (tabs.removeFromLeft (120));
    }
    r.removeFromTop (6);

    keyboard.setBounds (r.removeFromBottom (72));
    keyboard.setKeyWidth (keyboard.getWidth() / 36.0f);   // 36 white keys fill the width
    r.removeFromBottom (8);
    const int gap = 8, rowH = 112;

    // ---- FX page: three rows ----
    {
        auto fxArea = r;
        const int fxRowH = (fxArea.getHeight() - gap * 2) / 3;
        auto layoutRow = [&] (juce::Rectangle<int> row, std::initializer_list<std::pair<int, float>> items)
        {
            float total = 0; for (auto& it : items) total += it.second;
            const float avail = (float) (row.getWidth() - gap * ((int) items.size() - 1));
            for (auto& it : items) { fxPanels[it.first]->setBounds (row.removeFromLeft (juce::roundToInt (avail * it.second / total))); row.removeFromLeft (gap); }
        };
        layoutRow (fxArea.removeFromTop (fxRowH), { { 0, 4.4f }, { 1, 4.9f }, { 2, 4.9f } }); fxArea.removeFromTop (gap);
        layoutRow (fxArea.removeFromTop (fxRowH), { { 3, 3.0f }, { 4, 7.0f } });              fxArea.removeFromTop (gap);
        layoutRow (fxArea, { { 5, 4.9f }, { 6, 5.9f } });
    }
    // ---- ARP / MOD page ----
    {
        auto area = r;
        const int h = 132;
        modPanels[0]->setBounds (area.removeFromTop (h));
        area.removeFromTop (gap * 2);
        const int half = (area.getWidth() - gap) / 2;
        for (int row = 0; row < 2; ++row)
        {
            auto line = area.removeFromTop (h);
            modPanels[1 + row * 2]->setBounds (line.removeFromLeft (half));
            line.removeFromLeft (gap);
            modPanels[2 + row * 2]->setBounds (line);
            area.removeFromTop (gap);
        }
    }

    auto partsArea = r.removeFromTop (rowH * 3 + gap * 2);
    const int colW = (partsArea.getWidth() - gap) / 2;
    for (int part = 0; part < 2; ++part)
    {
        auto col = juce::Rectangle<int> (partsArea.getX() + part * (colW + gap), partsArea.getY(), colW, partsArea.getHeight());
        panels[part * 2]->setBounds (col.removeFromTop (rowH));     col.removeFromTop (gap);
        panels[part * 2 + 1]->setBounds (col.removeFromTop (rowH)); col.removeFromTop (gap);
        auto row = col.removeFromTop (rowH);
        panels[4 + part * 2]->setBounds (row.removeFromLeft ((int) (row.getWidth() * 0.6f)));
        row.removeFromLeft (gap);
        panels[5 + part * 2]->setBounds (row);
    }
    r.removeFromTop (gap);
    auto modRow = r.removeFromTop (rowH);
    const int half = (modRow.getWidth() - gap) / 2;
    panels[8]->setBounds (modRow.removeFromLeft (half)); modRow.removeFromLeft (gap);
    panels[9]->setBounds (modRow);
    r.removeFromTop (gap);
    auto lfoRow = r.removeFromTop (rowH);
    const int third = (lfoRow.getWidth() - gap * 2) / 3;
    panels[10]->setBounds (lfoRow.removeFromLeft (third)); lfoRow.removeFromLeft (gap);
    panels[11]->setBounds (lfoRow.removeFromLeft (third)); lfoRow.removeFromLeft (gap);
    panels[12]->setBounds (lfoRow);
}
