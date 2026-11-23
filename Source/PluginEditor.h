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
    enum class DragTarget { none, volume, parameter, value };
    void timerCallback() override;
    void updateFromDrag(float y);
    void selectNextParameter(int delta);
    void changeSelectedValue(int delta);
    QuasimidiTechnoXAudioProcessor& audioProcessor;
    std::unique_ptr<juce::Drawable> frontPanel;
    DragTarget dragTarget = DragTarget::none;
    float dragStartY = 0.0f;
    float dragStartValue = 0.0f;
    int selectedPart = 0;
    int selectedParameter = 3;
    bool editMode = false;
    bool buttonFlash = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuasimidiTechnoXAudioProcessorEditor)
};
