#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

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

    QuasimidiTechnoXAudioProcessor& audioProcessor;
    std::unique_ptr<juce::Drawable> frontPanel;
    juce::ComboBox performanceSelector;
    juce::Label performanceLabel;
    int partNumber = 1;
    int midiChannel = 1;
    int parameterNumber = 1;
    int valueNumber = 64;
    int volumeNumber = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuasimidiTechnoXAudioProcessorEditor)
};
