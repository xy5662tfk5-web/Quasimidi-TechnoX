#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

namespace {
constexpr int kStateMagicV1 = 0x54584E50; // TXNP
constexpr int kStateMagicV2 = 0x54584E32; // TXN2: Part values + MIDI channels
constexpr int kStateMagicV3 = 0x54584E33; // TXN3: Part + Common values + MIDI channels
constexpr int kPartStateBytesV2 = 50 * 4 * 24 + 4;
constexpr int kPartStateBytesV3 = 50 * 4 * 24 + 50 * 4 + 4;
}

QuasimidiTechnoXAudioProcessor::QuasimidiTechnoXAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    for (auto& performance : partValues)
        for (auto& part : performance)
            for (auto& parameter : part)
                parameter.store(0, std::memory_order_relaxed);
    for (auto& performance : commonValues)
        for (auto& value : performance)
            value.store(-1, std::memory_order_relaxed);
    for (std::size_t part = 0; part < partMidiChannels.size(); ++part)
        partMidiChannels[part].store(static_cast<int>(part + 13), std::memory_order_relaxed);

    int midiSize = 0;
    int catalogSize = 0;
    if (const auto* midiData = BinaryData::getNamedResource("TECHNOX_MID", midiSize))
        performanceBank.loadFromMidi(midiData, midiSize);
    if (const auto* catalogData = BinaryData::getNamedResource("technox_ins_json", catalogSize))
        soundCatalog.loadFromJson(catalogData, catalogSize);
    initialisePartStates();
    syncCurrentPerformanceToEngine();
}

void QuasimidiTechnoXAudioProcessor::initialisePartStates()
{
    for (std::size_t performanceIndex = 0; performanceIndex < performanceBank.count(); ++performanceIndex) {
        const auto* performance = performanceBank.at(performanceIndex);
        if (performance == nullptr)
            continue;
        for (std::size_t partIndex = 0; partIndex < 4; ++partIndex) {
            const auto model = performance->makePartEditModel(partIndex);
            for (std::size_t parameter = 0; parameter < 24; ++parameter) {
                const int value = model.rawValue(parameter);
                if (value >= 0)
                    partValues[performanceIndex][partIndex][parameter].store(value, std::memory_order_relaxed);
            }
        }
    }
}

void QuasimidiTechnoXAudioProcessor::syncCurrentPerformanceToEngine() noexcept
{
    const int performanceIndex = getCurrentProgram();
    if (performanceIndex < 0 || performanceIndex >= 50)
        return;
    for (int slot = 0; slot < 4; ++slot) {
        for (std::size_t parameter = 0; parameter < 24; ++parameter) {
            const int value = getPartParameterRaw(performanceIndex, slot,
                static_cast<technox::PartParameter>(parameter));
            if (value >= 0)
                massEngine.setPartParameter(12 + slot,
                    static_cast<technox::PartParameter>(parameter), value);
        }
    }
    constexpr technox::MASSCommonControl controls[] = {
        technox::MASSCommonControl::fx1Type, technox::MASSCommonControl::fx2Type,
        technox::MASSCommonControl::arpResolution, technox::MASSCommonControl::arpSpeed
    };
    for (std::size_t i = 0; i < 4; ++i) {
        const int value = commonValues[static_cast<std::size_t>(performanceIndex)][i]
            .load(std::memory_order_acquire);
        if (value >= 0)
            massEngine.setCommonControl(controls[i], value);
        else
            massEngine.clearCommonControl(controls[i]);
    }
}

bool QuasimidiTechnoXAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void QuasimidiTechnoXAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    massEngine.prepare(sampleRate, samplesPerBlock);
}
void QuasimidiTechnoXAudioProcessor::releaseResources() {}

void QuasimidiTechnoXAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int performanceIndex = getCurrentProgram();

    // MIDI controller decoding is bounded and allocation-free. The audio path
    // only publishes parameter values atomically; the editor reads the same state.
    for (const auto metadata : midiMessages) {
        const auto& message = metadata.getMessage();
        const int slot = partSlotForMidiChannel(message.getChannel());
        if (slot < 0 || performanceIndex < 0 || performanceIndex >= 50)
            continue;

        if (message.isNoteOn()) {
            massEngine.noteOn(static_cast<std::uint8_t>(12 + slot), static_cast<std::uint8_t>(message.getChannel()),
                              static_cast<std::uint8_t>(message.getNoteNumber()),
                              static_cast<std::uint8_t>(message.getVelocity()));
        } else if (message.isNoteOff()) {
            massEngine.noteOff(static_cast<std::uint8_t>(message.getChannel()),
                               static_cast<std::uint8_t>(message.getNoteNumber()));
        } else if (message.isAllSoundOff()) {
            massEngine.allSoundsOff(message.getChannel());
        } else if (message.isAllNotesOff()) {
            massEngine.allNotesOff(message.getChannel());
        } else if (message.isController()) {
            const auto change = midiControlRouter.handleControlChange(
                message.getChannel(), message.getControllerNumber(), message.getControllerValue());
            if (message.getControllerNumber() == 80)
                setCommonControlRaw(performanceIndex, technox::MASSCommonControl::fx1Type, message.getControllerValue());
            else if (message.getControllerNumber() == 81)
                setCommonControlRaw(performanceIndex, technox::MASSCommonControl::fx2Type, message.getControllerValue());
            else if (message.getControllerNumber() == 82)
                setCommonControlRaw(performanceIndex, technox::MASSCommonControl::arpResolution, message.getControllerValue());
            else if (message.getControllerNumber() == 83)
                setCommonControlRaw(performanceIndex, technox::MASSCommonControl::arpSpeed, message.getControllerValue());
            else if (message.getControllerNumber() == 1)
                massEngine.setModWheel(12 + slot, message.getControllerValue());
            else if (message.getControllerNumber() == 65)
                massEngine.setPortamento(12 + slot, message.getControllerValue() >= 64);
            else if (message.getControllerNumber() == 67)
                massEngine.setSoftPedal(12 + slot, message.getControllerValue() >= 64);
            else if (message.getControllerNumber() == 121)
                massEngine.resetPartControllers(12 + slot);
            if (change.has_value())
                setPartParameterRaw(performanceIndex, slot, change->parameter, change->rawValue);
        } else if (message.isProgramChange()) {
            setPartParameterRaw(performanceIndex, slot, technox::PartParameter::soundNumber,
                                message.getProgramChangeNumber());
        }
    }

    // The M.A.S.S. synthesis backend is not implemented yet. MIDI/state editing
    // is active, but audio remains intentionally silent until verified DSP exists.
    massEngine.render(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void QuasimidiTechnoXAudioProcessor::setCurrentProgram(int index)
{
    if (index >= 0 && performanceBank.select(static_cast<std::size_t>(index)))
        syncCurrentPerformanceToEngine();
}

const juce::String QuasimidiTechnoXAudioProcessor::getProgramName(int index)
{
    const auto* performance = index >= 0
        ? performanceBank.at(static_cast<std::size_t>(index)) : nullptr;
    return performance != nullptr ? juce::String::fromUTF8(performance->name.c_str())
                                   : juce::String();
}

juce::String QuasimidiTechnoXAudioProcessor::getCurrentPerformanceName() const
{
    const auto* performance = performanceBank.current();
    return performance != nullptr ? juce::String::fromUTF8(performance->name.c_str())
                                   : juce::String("Bank not loaded");
}

int QuasimidiTechnoXAudioProcessor::getPartParameterRaw(
    int performanceIndex, int partIndex, technox::PartParameter parameter) const noexcept
{
    const auto parameterIndex = static_cast<std::size_t>(parameter);
    if (performanceIndex < 0 || performanceIndex >= 50 || partIndex < 0 || partIndex >= 4 ||
        parameterIndex >= 24)
        return -1;
    return partValues[static_cast<std::size_t>(performanceIndex)]
                     [static_cast<std::size_t>(partIndex)][parameterIndex]
                         .load(std::memory_order_acquire);
}

bool QuasimidiTechnoXAudioProcessor::setPartParameterRaw(
    int performanceIndex, int partIndex, technox::PartParameter parameter, int rawValue) noexcept
{
    const auto parameterIndex = static_cast<std::size_t>(parameter);
    const auto* descriptor = technox::PartEditModel::descriptor(parameterIndex);
    if (performanceIndex < 0 || performanceIndex >= 50 || partIndex < 0 || partIndex >= 4 ||
        descriptor == nullptr || rawValue < 0 || rawValue > descriptor->maximum)
        return false;
    partValues[static_cast<std::size_t>(performanceIndex)]
              [static_cast<std::size_t>(partIndex)][parameterIndex]
                  .store(rawValue, std::memory_order_release);
    if (performanceIndex == getCurrentProgram())
        massEngine.setPartParameter(12 + partIndex, parameter, rawValue);
    return true;
}

int QuasimidiTechnoXAudioProcessor::getCommonControlRaw(
    int performanceIndex, technox::MASSCommonControl control) const noexcept
{
    const auto index = static_cast<std::size_t>(control);
    if (performanceIndex < 0 || performanceIndex >= 50 || index >= 4)
        return -1;
    return commonValues[static_cast<std::size_t>(performanceIndex)][index].load(std::memory_order_acquire);
}

bool QuasimidiTechnoXAudioProcessor::setCommonControlRaw(
    int performanceIndex, technox::MASSCommonControl control, int rawValue) noexcept
{
    const auto index = static_cast<std::size_t>(control);
    constexpr int maxima[4] = {21, 21, 3, 127};
    if (performanceIndex < 0 || performanceIndex >= 50 || index >= 4 ||
        rawValue < 0 || rawValue > maxima[index])
        return false;
    commonValues[static_cast<std::size_t>(performanceIndex)][index].store(rawValue, std::memory_order_release);
    if (performanceIndex == getCurrentProgram())
        massEngine.setCommonControl(control, rawValue);
    return true;
}

int QuasimidiTechnoXAudioProcessor::getPartMidiChannel(int partIndex) const noexcept
{
    if (partIndex < 0 || partIndex >= 4)
        return 0;
    return partMidiChannels[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire);
}

bool QuasimidiTechnoXAudioProcessor::setPartMidiChannel(int partIndex, int midiChannel) noexcept
{
    if (partIndex < 0 || partIndex >= 4 || midiChannel < 1 || midiChannel > 16)
        return false;
    for (int other = 0; other < 4; ++other)
        if (other != partIndex && getPartMidiChannel(other) == midiChannel)
            return false;
    partMidiChannels[static_cast<std::size_t>(partIndex)].store(midiChannel, std::memory_order_release);
    return true;
}

int QuasimidiTechnoXAudioProcessor::partSlotForMidiChannel(int channel) const noexcept
{
    for (int slot = 0; slot < 4; ++slot)
        if (getPartMidiChannel(slot) == channel)
            return slot;
    return -1;
}

bool QuasimidiTechnoXAudioProcessor::getEditedPartSysEx(
    int performanceIndex, int partIndex, std::vector<std::uint8_t>& destination) const
{
    const auto* performance = performanceBank.at(performanceIndex < 0 ? 50 : static_cast<std::size_t>(performanceIndex));
    if (performance == nullptr || partIndex < 0 || partIndex >= 4)
        return false;
    auto model = performance->makePartEditModel(static_cast<std::size_t>(partIndex));
    if (!model.isLoaded())
        return false;
    for (std::size_t parameter = 0; parameter < 24; ++parameter) {
        const int value = getPartParameterRaw(performanceIndex, partIndex,
            static_cast<technox::PartParameter>(parameter));
        if (value < 0 || !model.setRawValue(parameter, static_cast<std::uint8_t>(value)))
            return false;
    }
    destination = model.editedSysEx();
    return true;
}

juce::String QuasimidiTechnoXAudioProcessor::getCurrentPartSoundName(int partIndex) const
{
    const int performance = getCurrentProgram();
    const int bank = getPartParameterRaw(performance, partIndex, technox::PartParameter::bankNumber);
    const int sound = getPartParameterRaw(performance, partIndex, technox::PartParameter::soundNumber);
    return soundCatalog.nameFor(bank, sound);
}

juce::AudioProcessorEditor* QuasimidiTechnoXAudioProcessor::createEditor()
{
    return new QuasimidiTechnoXAudioProcessorEditor(*this);
}

void QuasimidiTechnoXAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream(destData, false);
    stream.writeInt(kStateMagicV3);
    stream.writeInt(getCurrentProgram());
    for (const auto& performance : partValues)
        for (const auto& part : performance)
            for (const auto& parameter : part)
                stream.writeByte(static_cast<char>(parameter.load(std::memory_order_acquire)));
    for (const auto& performance : commonValues)
        for (const auto& value : performance)
            stream.writeByte(static_cast<char>(value.load(std::memory_order_acquire)));
    for (const auto& channel : partMidiChannels)
        stream.writeByte(static_cast<char>(channel.load(std::memory_order_acquire)));
}

void QuasimidiTechnoXAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes < 8)
        return;
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
    const int magic = stream.readInt();
    const int program = stream.readInt();
    if (magic == kStateMagicV1) {
        setCurrentProgram(program);
        return;
    }
    if (magic != kStateMagicV2 && magic != kStateMagicV3)
        return;
    const int requiredBytes = magic == kStateMagicV2 ? kPartStateBytesV2 : kPartStateBytesV3;
    if (sizeInBytes < 8 + requiredBytes)
        return;

    for (std::size_t performance = 0; performance < partValues.size(); ++performance) {
        for (std::size_t part = 0; part < partValues[performance].size(); ++part) {
            for (std::size_t parameter = 0; parameter < partValues[performance][part].size(); ++parameter) {
                const int value = static_cast<unsigned char>(stream.readByte());
                const auto maximum = technox::kPartParameterDescriptors[parameter].maximum;
                partValues[performance][part][parameter].store(
                    juce::jlimit(0, static_cast<int>(maximum), value), std::memory_order_release);
            }
        }
    }

    if (magic == kStateMagicV3) {
        constexpr int commonMaxima[4] = {21, 21, 3, 127};
        for (std::size_t performance = 0; performance < commonValues.size(); ++performance)
            for (std::size_t control = 0; control < 4; ++control) {
                const int value = static_cast<unsigned char>(stream.readByte());
                commonValues[performance][control].store(
                    value == 0xff ? -1 : juce::jlimit(0, commonMaxima[control], value),
                    std::memory_order_release);
            }
    } else {
        for (auto& performance : commonValues)
            for (auto& value : performance)
                value.store(-1, std::memory_order_release);
    }

    for (auto& channel : partMidiChannels) {
        const int restoredChannel = static_cast<unsigned char>(stream.readByte());
        if (restoredChannel >= 1 && restoredChannel <= 16)
            channel.store(restoredChannel, std::memory_order_release);
    }
    setCurrentProgram(program);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QuasimidiTechnoXAudioProcessor();
}
