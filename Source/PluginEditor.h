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
    enum class EditorPage { main, editFunctionMenu, editPart, editSystem };
    void timerCallback() override;
    void updateFromDrag(float y);
    void selectNextParameter(int delta);
    void changeSelectedValue(int delta);
    void syncValueDialFromSelection() noexcept;
    void syncParameterDialFromSelection() noexcept;
    void syncDialsFromCurrentSelection() noexcept;
    technox::PanelOperatingMode panelOperatingMode() const noexcept;
    technox::PanelEditPage panelEditPage() const noexcept;
    int selectedPerformanceForDial() const noexcept;
    int currentPartParameterRaw(technox::PartParameter parameter) const noexcept;
    bool queueCurrentPartParameterRaw(technox::PartParameter parameter, int rawValue) noexcept;
    juce::String currentPartSoundName() const;
    int currentPartDisplayNumber() const noexcept;
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
    int lastPerformanceIndex = 0;
    EditorPage page = EditorPage::main;
    int selectedEditFunction = 2; // <2> Edit Part is the implemented default.
    int selectedSystemParameter = 0; // 0 = Mode (Perform / Sequencer Multi).
    bool menuNotImplemented = false;
    int selectedPart = 0; // Performance's stored Part blocks 13–16.
    int selectedSequencerPart = 0; // Sequencer Multi Parts 1–16.
    int selectedParameter = 3;
    bool buttonFlash = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuasimidiTechnoXAudioProcessorEditor)
};
