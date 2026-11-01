#include "PluginEditor.h"

namespace colours
{
    const juce::Colour bg      { 0xff15131a };
    const juce::Colour panel   { 0xff1f1c26 };
    const juce::Colour track   { 0xff2e2a37 };
    const juce::Colour accent  { 0xffe8b24a };
    const juce::Colour accent2 { 0xffc9668a };
    const juce::Colour text    { 0xffece6da };
    const juce::Colour dim     { 0xff8d8698 };
}

//==============================================================================
KoorLookAndFeel::KoorLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, colours::panel);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
}

void KoorLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float thickness = juce::jmax (3.f, radius * 0.13f);
    const float arcR = radius - thickness * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path bgArc;
    bgArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.f, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (bgArc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.001f)
    {
        juce::Path valArc;
        valArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.f, startAngle, angle, true);
        g.setGradientFill (juce::ColourGradient (colours::accent2, centre.x - radius, centre.y + radius,
                                                 colours::accent, centre.x + radius, centre.y - radius, false));
        g.strokePath (valArc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // knob body
    const float bodyR = arcR - thickness * 1.3f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff353041), centre.x, centre.y - bodyR,
                                             juce::Colour (0xff1b1921), centre.x, centre.y + bodyR, false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.f, bodyR * 2.f);

    // pointer
    juce::Path pointer;
    const float pl = bodyR * 0.62f, pw = juce::jmax (2.f, bodyR * 0.09f);
    pointer.addRoundedRectangle (-pw * 0.5f, -bodyR + bodyR * 0.16f, pw, pl * 0.55f, pw * 0.5f);
    g.setColour (colours::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void KoorLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.f);
    g.setColour (colours::accent.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 6.f, 1.f);

    juce::Path arrow;
    const float ax = (float) w - 18.f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 5.f, ay - 2.5f, ax + 5.f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (colours::accent);
    g.fillPath (arrow);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
            const juce::String& label, bool big)
    : text (label), isBig (big)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setDoubleClickReturnValue (true, state.getParameter (paramId)->convertFrom0to1 (
                                             state.getParameter (paramId)->getDefaultValue()));
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (isBig ? 26 : 20);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, r.getWidth(), isBig ? 24 : 18);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (isBig ? colours::accent : colours::dim);
    g.setFont (juce::FontOptions (isBig ? 17.f : 12.5f, juce::Font::bold));
    g.drawText (text.toUpperCase(), getLocalBounds().removeFromTop (isBig ? 26 : 20),
                juce::Justification::centred);
}

//==============================================================================
KoorEditor::KoorEditor (KoorProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      voices  (p.apvts, "voices",  "Stemmen", true),
      detune  (p.apvts, "detune",  "Ontstemming"),
      timing  (p.apvts, "timing",  "Timing"),
      vibrato (p.apvts, "vibrato", "Vibrato"),
      width   (p.apvts, "width",   "Breedte"),
      bright  (p.apvts, "bright",  "Helderheid"),
      octDown (p.apvts, "octdown", "Octaaf -1"),
      octUp   (p.apvts, "octup",   "Octaaf +1"),
      reverb  (p.apvts, "reverb",  "Galm"),
      mix     (p.apvts, "mix",     "Mix"),
      output  (p.apvts, "output",  "Uitvoer")
{
    setLookAndFeel (&lnf);

    for (auto* k : { &voices, &detune, &timing, &vibrato, &width, &bright,
                     &octDown, &octUp, &reverb, &mix, &output })
        addAndMakeVisible (*k);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0)
            proc.setCurrentProgram (presetBox.getSelectedId() - 1);
    };
    addAndMakeVisible (presetBox);

    setSize (820, 430);
    startTimerHz (30);
}

KoorEditor::~KoorEditor()
{
    setLookAndFeel (nullptr);
}

void KoorEditor::timerCallback()
{
    const float level = proc.meterLevel.load();
    const float target = juce::jlimit (0.f, 1.f, (juce::Decibels::gainToDecibels (level, -60.f) + 60.f) / 60.f);
    meter = target > meter ? target : meter * 0.9f + target * 0.1f;
    repaint (meterArea);

    const int n = (int) proc.apvts.getRawParameterValue ("voices")->load();
    if (n != lastVoices) { lastVoices = n; repaint(); }
    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

void KoorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);

    // header
    auto header = getLocalBounds().removeFromTop (70).reduced (24, 0);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (34.f, juce::Font::bold));
    g.drawText ("KOOR", header.removeFromLeft (120), juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (13.f));
    g.drawText (juce::String::fromUTF8 ("\xc3\xa9\xc3\xa9n stem  \xe2\x86\x92  een heel koor"),
                header.removeFromLeft (260), juce::Justification::centredLeft);

    // panels
    auto body = getLocalBounds().withTrimmedTop (70).reduced (16, 0).withTrimmedBottom (16);
    auto left = body.removeFromLeft (230).toFloat();
    body.removeFromLeft (12);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (left, 12.f);
    g.fillRoundedRectangle (body.toFloat(), 12.f);

    // little choir of dots that grows with the voice count
    const int n = (int) proc.apvts.getRawParameterValue ("voices")->load();
    auto dots = left.reduced (20.f).removeFromTop (34.f);
    for (int i = 0; i < 16; ++i)
    {
        const float dx = dots.getX() + (float) (i % 8) * (dots.getWidth() / 8.f) + 6.f;
        const float dy = dots.getY() + (float) (i / 8) * 16.f;
        g.setColour (i < n ? colours::accent : colours::track);
        g.fillEllipse (dx, dy, 9.f, 9.f);
    }

    // meter
    g.setColour (colours::track);
    g.fillRoundedRectangle (meterArea.toFloat(), 3.f);
    auto fill = meterArea.toFloat();
    fill = fill.removeFromLeft (fill.getWidth() * meter);
    g.setGradientFill (juce::ColourGradient (colours::accent2, fill.getX(), 0.f,
                                             colours::accent, (float) meterArea.getRight(), 0.f, false));
    g.fillRoundedRectangle (fill, 3.f);

    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (11.f));
    g.drawText ("KOOR-NIVEAU", meterArea.translated (0, -16).withHeight (14), juce::Justification::centredLeft);
}

void KoorEditor::resized()
{
    presetBox.setBounds (getWidth() - 24 - 220, 20, 220, 30);

    auto body = getLocalBounds().withTrimmedTop (70).reduced (16, 0).withTrimmedBottom (16);
    auto left = body.removeFromLeft (230);
    body.removeFromLeft (12);

    auto l = left.reduced (20);
    l.removeFromTop (40);
    meterArea = l.removeFromBottom (8);
    l.removeFromBottom (24);
    voices.setBounds (l);

    auto grid = body.reduced (12, 14);
    const int rowH = grid.getHeight() / 2;
    auto row1 = grid.removeFromTop (rowH);
    auto row2 = grid;

    auto place = [] (juce::Rectangle<int> row, std::initializer_list<Knob*> knobs)
    {
        const int w = row.getWidth() / (int) knobs.size();
        for (auto* k : knobs)
            k->setBounds (row.removeFromLeft (w).reduced (4));
    };

    place (row1, { &detune, &timing, &vibrato, &width, &bright });
    place (row2, { &octDown, &octUp, &reverb, &mix, &output });
}
