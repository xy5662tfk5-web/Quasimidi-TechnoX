#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TechnoXSysEx.h"

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
    // Consume documented MIDI state changes. Audio generation remains disabled
    // until the original sample format and synthesis architecture are verified.
    juce::MidiBuffer::Iterator midiIterator(midiMessages);
    juce::MidiMessage midiMessage;
    int samplePosition = 0;
    while (midiIterator.getNextEvent(midiMessage, samplePosition))
    {
        if (midiMessage.isSysEx())
        {
            const auto* raw = midiMessage.getRawData();
            const auto rawSize = midiMessage.getRawDataSize();
            if (raw != nullptr && rawSize > 0)
            {
                std::vector<std::uint8_t> packet(raw, raw + rawSize);
                // Parse requests/dumps defensively; address-space routing and
                // memory mutation will be added with the complete SysEx model.
                (void) technox::parseRequest(packet);
                (void) technox::parseDump(packet);
            }
        }
        else
        {
            midiState.handle(midiMessage.getRawData(), midiMessage.getRawDataSize());
        }
    }

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
