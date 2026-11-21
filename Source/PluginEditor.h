#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>

class QuasimidiTechnoXAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                   private juce::Timer
{
public:
    explicit QuasimidiTechnoXAudioProcessorEditor(QuasimidiTechnoXAudioProcessor&);
    ~QuasimidiTechnoXAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void refreshPartControls();

    QuasimidiTechnoXAudioProcessor& audioProcessor;
    std::unique_ptr<juce::Drawable> frontPanel;
    juce::ComboBox performanceSelector;
    juce::Label performanceLabel;
    juce::ComboBox partSelector;
    juce::ComboBox midiChannelSelector;
    juce::Label partLabel;
    juce::Label midiChannelLabel;
    std::array<juce::Slider, 24> parameterSliders;
    std::array<juce::Label, 24> parameterLabels;
    int selectedPartSlot = 0;
    int lastDisplayedPerformance = -1;
    bool refreshingControls = false;
    int partNumber = 13;
    int midiChannel = 1;
    int parameterNumber = 1;
    int valueNumber = 64;
    int volumeNumber = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuasimidiTechnoXAudioProcessorEditor)
};
