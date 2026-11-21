#pragma once
#include <JuceHeader.h>
#include "TechnoXPerformanceBank.h"
#include "TechnoXSoundCatalog.h"

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
    bool producesMidi() const override { return true; }
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
    const technox::SoundCatalog& getSoundCatalog() const noexcept { return soundCatalog; }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    technox::PerformanceBank performanceBank;
    technox::SoundCatalog soundCatalog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuasimidiTechnoXAudioProcessor)
};
