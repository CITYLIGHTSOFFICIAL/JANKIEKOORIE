#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class KoorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KoorLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
          const juce::String& label, bool big = false);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::String text;
    bool isBig;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class KoorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KoorEditor (KoorProcessor&);
    ~KoorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    KoorProcessor& proc;
    KoorLookAndFeel lnf;

    Knob voices, detune, timing, vibrato, width, bright, octDown, octUp, reverb, mix, output;
    juce::ComboBox presetBox;
    float meter = 0.f;
    int lastVoices = -1;
    juce::Rectangle<int> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KoorEditor)
};
