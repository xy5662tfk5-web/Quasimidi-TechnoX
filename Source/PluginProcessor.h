#pragma once
#include <JuceHeader.h>
#include "TechnoXPerformanceBank.h"
#include "TechnoXSoundCatalog.h"
#include "TechnoXMidiControlRouter.h"
#include "MASSAudioEngine.h"
#include <array>
#include <atomic>
#include <vector>

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return static_cast<int>(performanceBank.count()); }
    int getCurrentProgram() override { return static_cast<int>(performanceBank.selectedIndex()); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    const technox::PerformanceBank& getPerformanceBank() const noexcept { return performanceBank; }
    juce::String getCurrentPerformanceName() const;
    juce::String getCurrentPartSoundName(int partIndex) const;
    int getPartParameterRaw(int performanceIndex, int partIndex, technox::PartParameter parameter) const noexcept;
    bool setPartParameterRaw(int performanceIndex, int partIndex, technox::PartParameter parameter, int rawValue) noexcept;
    int getCommonControlRaw(int performanceIndex, technox::MASSCommonControl control) const noexcept;
    bool setCommonControlRaw(int performanceIndex, technox::MASSCommonControl control, int rawValue) noexcept;
    bool getEditedPartSysEx(int performanceIndex, int partIndex, std::vector<std::uint8_t>& destination) const;
    int getPartMidiChannel(int partIndex) const noexcept;
    bool setPartMidiChannel(int partIndex, int midiChannel) noexcept;
    const technox::SoundCatalog& getSoundCatalog() const noexcept { return soundCatalog; }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    technox::PerformanceBank performanceBank;
    technox::SoundCatalog soundCatalog;
    technox::MidiControlRouter midiControlRouter;
    technox::MASSAudioEngine massEngine;
    std::array<std::array<std::array<std::atomic<int>, 24>, 4>, 50> partValues{};
    std::array<std::array<std::atomic<int>, 4>, 50> commonValues{};
    std::array<std::atomic<int>, 4> partMidiChannels{};

    void initialisePartStates();
    void initialiseCommonControlsFromBank() noexcept;
    void syncCurrentPerformanceToEngine() noexcept;
    int partSlotForMidiChannel(int channel) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuasimidiTechnoXAudioProcessor)
};
