#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

QuasimidiTechnoXAudioProcessor::QuasimidiTechnoXAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    int midiSize = 0;
    int catalogSize = 0;
    if (const auto* midiData = BinaryData::getNamedResource("TECHNOX_MID", midiSize))
        performanceBank.loadFromMidi(midiData, midiSize);
    if (const auto* catalogData = BinaryData::getNamedResource("technox_ins_json", catalogSize))
        soundCatalog.loadFromJson(catalogData, catalogSize);
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
    // The Performance bank is available, but the proprietary synthesis engine
    // is not implemented yet. Keep the plugin intentionally silent.
    buffer.clear();
}

void QuasimidiTechnoXAudioProcessor::setCurrentProgram(int index)
{
    performanceBank.select(static_cast<std::size_t>(juce::jmax(0, index)));
}

const juce::String QuasimidiTechnoXAudioProcessor::getProgramName(int index)
{
    const auto* performance = performanceBank.at(static_cast<std::size_t>(juce::jmax(0, index)));
    return performance != nullptr ? juce::String::fromUTF8(performance->name.c_str())
                                   : juce::String();
}

juce::String QuasimidiTechnoXAudioProcessor::getCurrentPerformanceName() const
{
    const auto* performance = performanceBank.current();
    return performance != nullptr ? juce::String::fromUTF8(performance->name.c_str())
                                   : juce::String("Bank not loaded");
}

juce::AudioProcessorEditor* QuasimidiTechnoXAudioProcessor::createEditor()
{
    return new QuasimidiTechnoXAudioProcessorEditor(*this);
}

void QuasimidiTechnoXAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream(destData, false);
    stream.writeInt(0x54584E50); // "TXNP"
    stream.writeInt(getCurrentProgram());
}

void QuasimidiTechnoXAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes < 8) return;
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
    if (stream.readInt() != 0x54584E50) return;
    setCurrentProgram(stream.readInt());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QuasimidiTechnoXAudioProcessor();
}
