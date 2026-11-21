#include "PluginProcessor.h"
#include "PluginEditor.h"

QuasimidiTechnoXAudioProcessor::QuasimidiTechnoXAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool QuasimidiTechnoXAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void QuasimidiTechnoXAudioProcessor::prepareToPlay(double, int) {}
void QuasimidiTechnoXAudioProcessor::releaseResources() {}

void QuasimidiTechnoXAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    // Deliberately silent foundation: the original TechnoX synthesis engine
    // has not yet been researched or implemented.
    buffer.clear();
}

juce::AudioProcessorEditor* QuasimidiTechnoXAudioProcessor::createEditor()
{
    return new QuasimidiTechnoXAudioProcessorEditor(*this);
}

void QuasimidiTechnoXAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    destData.reset();
}

void QuasimidiTechnoXAudioProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QuasimidiTechnoXAudioProcessor();
}
