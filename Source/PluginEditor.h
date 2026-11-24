#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "TechnoXPanelControlMap.h"

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
    void syncValueDialFromSelection() noexcept;
    void syncParameterDialFromSelection() noexcept;
    void syncDialsFromCurrentSelection() noexcept;
    technox::PanelOperatingMode panelOperatingMode() const noexcept;
    int selectedPerformanceForDial() const noexcept;
    QuasimidiTechnoXAudioProcessor& audioProcessor;
    std::unique_ptr<juce::Drawable> frontPanel;
    DragTarget dragTarget = DragTarget::none;
    float dragStartY = 0.0f;
    float parameterDragStartIndex = 0.0f;
    float valueDragStartRaw = 0.0f;
    float volumeDragStartValue = 0.0f;
    // The VALUE dial has its own displayed state. It is frozen while the
    // PARAMETER encoder is being moved, then synchronised when that gesture ends.
    float parameterDialNormalized = 0.0f;
    float valueDialNormalized = 0.0f;
    bool ramPerformanceBank = false;
    int ramPerformanceSelection = 0;
    MidiRoutingMode lastRoutingMode = MidiRoutingMode::performanceOmni;
    int selectedPart = 0;
    int selectedParameter = 3;
    bool editMode = false;
    bool buttonFlash = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuasimidiTechnoXAudioProcessorEditor)
};
