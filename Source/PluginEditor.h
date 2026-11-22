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
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void updateVolumeFromDrag(float y);

    QuasimidiTechnoXAudioProcessor& audioProcessor;
    std::unique_ptr<juce::Drawable> frontPanel;
    bool draggingVolume = false;
    float volumeAtDragStart = 0.8f;
    float dragStartY = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuasimidiTechnoXAudioProcessorEditor)
};
