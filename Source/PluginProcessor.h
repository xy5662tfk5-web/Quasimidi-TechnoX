#pragma once
#include <JuceHeader.h>
#include "TechnoXPerformanceBank.h"
#include "TechnoXSoundCatalog.h"
#include "TechnoXMidiControlRouter.h"
#include "MASSAudioEngine.h"
#include "MASSResourceROM.h"
#include "BasicMASSVoiceBackend.h"
#include "TechnoX8032Machine.h"
#include "TechnoXPanelControlEvents.h"
#include <array>
#include <atomic>
#include <vector>
#include <memory>

enum class MidiRoutingMode : int { performanceOmni = 0, partChannels = 1 };

class QuasimidiTechnoXAudioProcessor final : public juce::AudioProcessor
{
public:
    QuasimidiTechnoXAudioProcessor();
    ~QuasimidiTechnoXAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return static_cast<int>(performanceBank.count()); }
    int getCurrentProgram() override { return static_cast<int>(performanceBank.selectedIndex()); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    const technox::PerformanceBank& getPerformanceBank() const noexcept { return performanceBank; }
    juce::String getCurrentPerformanceName() const;
    float getOutputGain() const noexcept { return outputGain.load(std::memory_order_acquire); }
    void setOutputGain(float value) noexcept { outputGain.store(juce::jlimit(0.0f, 1.0f, value), std::memory_order_release); }
    juce::String getCurrentPartSoundName(int partIndex) const;
    juce::String getCurrentPartResourceSummary(int partIndex) const;
    int getPartParameterRaw(int performanceIndex, int partIndex, technox::PartParameter parameter) const noexcept;
    bool setPartParameterRaw(int performanceIndex, int partIndex, technox::PartParameter parameter, int rawValue) noexcept;
    bool queuePanelPartParameterRaw(int performanceIndex, int partIndex, technox::PartParameter parameter, int rawValue) noexcept;
    int getSequencerPartParameterRaw(int partIndex, technox::PartParameter parameter) const noexcept;
    bool setSequencerPartParameterRaw(int partIndex, technox::PartParameter parameter, int rawValue) noexcept;
    bool queueSequencerPartParameterRaw(int partIndex, technox::PartParameter parameter, int rawValue) noexcept;
    juce::String getSequencerPartSoundName(int partIndex) const;
    int getSequencerDrumSet() const noexcept;
    bool queueSequencerDrumSet(int drumSet) noexcept;
    int getCommonControlRaw(int performanceIndex, technox::MASSCommonControl control) const noexcept;
    bool setCommonControlRaw(int performanceIndex, technox::MASSCommonControl control, int rawValue) noexcept;
    bool getEditedPartSysEx(int performanceIndex, int partIndex, std::vector<std::uint8_t>& destination) const;
    int getPartMidiChannel(int partIndex) const noexcept;
    bool setPartMidiChannel(int partIndex, int midiChannel) noexcept;
    MidiRoutingMode getMidiRoutingMode() const noexcept;
    void setMidiRoutingMode(MidiRoutingMode mode) noexcept;
    bool isPerformancePartActive(int partIndex) const noexcept;
    const technox::SoundCatalog& getSoundCatalog() const noexcept { return soundCatalog; }

    // Call while audio processing is stopped. Ownership is transferred to the
    // processor; this is the integration point for the recovered original DSP.
    bool installVoiceBackend(std::unique_ptr<technox::IMASSVoiceBackend> backend) noexcept;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    technox::PerformanceBank performanceBank;
    technox::SoundCatalog soundCatalog;
    technox::MASSResourceROM resourceROM;
    technox::MASSIC51ExternalSoundMemoryImage externalSoundMemoryImage;
    technox::MASSResourceProgramCatalog resourceProgramCatalog;
    technox::MASSFX1ResourceCatalog resourceFX1Catalog;
    technox::MASSFX2ResourceCatalog resourceFX2Catalog;
    std::unique_ptr<technox::TechnoX8032Machine> firmwareMachine;
    mutable std::array<technox::MASSResolvedProgramResource, 4> resolvedResources{};
    mutable std::array<int, 4> resolvedResourceBanks{{-1, -1, -1, -1}};
    mutable std::array<int, 4> resolvedResourcePrograms{{-1, -1, -1, -1}};
    mutable std::array<bool, 4> resolvedResourceValid{};
    technox::MidiControlRouter midiControlRouter;
    std::unique_ptr<technox::IMASSVoiceBackend> voiceBackend;
    technox::MASSAudioEngine massEngine;
    technox::PanelPartEditMailbox panelPartEditMailbox;
    technox::PanelSequencerPartEditMailbox panelSequencerPartEditMailbox;
    std::array<std::array<std::array<std::atomic<int>, 24>, 4>, 50> partValues{};
    std::array<std::array<std::atomic<int>, 24>, 16> sequencerPartValues{};
    std::atomic<int> sequencerDrumSet{0};
    std::atomic<int> pendingSequencerDrumSet{-1};
    std::array<std::array<std::atomic<int>, 5>, 50> commonValues{};
    std::atomic<int> midiRoutingMode{static_cast<int>(MidiRoutingMode::performanceOmni)};
    std::atomic<bool> pendingPerformanceSync{false};
    std::atomic<bool> pendingSequencerSync{false};
    std::atomic<float> outputGain{0.8f};
    double firmwareCycleTarget = 0.0;
    std::array<std::atomic<int>, 16> partMidiChannels{};

    void initialisePartStates();
    void initialiseSequencerPartStates() noexcept;
    void initialiseCommonControlsFromBank() noexcept;
    void syncCurrentPerformanceToEngine() noexcept;
    void syncSequencerPartsToEngine() noexcept;
    int partSlotForMidiChannel(int channel) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuasimidiTechnoXAudioProcessor)
};
