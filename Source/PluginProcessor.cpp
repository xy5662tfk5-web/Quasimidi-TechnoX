#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TechnoXPerformanceLayers.h"
#include "BinaryData.h"

namespace {
constexpr int kStateMagicV1 = 0x54584E50; // TXNP
constexpr int kStateMagicV2 = 0x54584E32; // TXN2: Part values + MIDI channels
constexpr int kStateMagicV3 = 0x54584E33; // TXN3: Part + four Common values + MIDI channels
constexpr int kStateMagicV4 = 0x54584E34; // TXN4: Part + five Common values + MIDI channels + routing mode
constexpr int kPartStateBytesV2 = 50 * 4 * 24 + 4;
constexpr int kPartStateBytesV3 = 50 * 4 * 24 + 50 * 4 + 4;
constexpr int kPartStateBytesV4 = 50 * 4 * 24 + 50 * 5 + 4 + 1;
}

QuasimidiTechnoXAudioProcessor::QuasimidiTechnoXAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      voiceBackend(std::make_unique<technox::BasicMASSVoiceBackend>()),
      massEngine(voiceBackend.get())
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
    int resourceRomSize = 0;
    if (const auto* midiData = BinaryData::getNamedResource("TECHNOX_MID", midiSize))
        performanceBank.loadFromMidi(midiData, midiSize);
    if (const auto* catalogData = BinaryData::getNamedResource("technox_ins_json", catalogSize))
        soundCatalog.loadFromJson(catalogData, catalogSize);
    const auto* resourceData = BinaryData::getNamedResource(
        "technox_IC51_nm27c010_v105_bin", resourceRomSize);
    if (resourceData != nullptr &&
        resourceROM.load(reinterpret_cast<const std::uint8_t*>(resourceData),
                         static_cast<std::size_t>(resourceRomSize)) &&
        resourceProgramCatalog.load(resourceROM)) {
        // The catalog is immutable after construction. Every voice backend can
        // resolve its captured bank/program pair without parsing or allocating
        // on the audio thread.
        massEngine.setProgramCatalog(&resourceProgramCatalog);
    }
    int firmwareRomSize = 0;
    if (const auto* firmwareData = BinaryData::getNamedResource(
            "technox_IC4_mx27c1000_v105_bin", firmwareRomSize)) {
        firmwareMachine = std::make_unique<technox::TechnoX8032Machine>();
        if (firmwareMachine->loadIC4Rom(
                reinterpret_cast<const std::uint8_t*>(firmwareData),
                static_cast<std::size_t>(firmwareRomSize))) {
            if (resourceData != nullptr &&
                !firmwareMachine->loadIC51Rom(
                    reinterpret_cast<const std::uint8_t*>(resourceData),
                    static_cast<std::size_t>(resourceRomSize))) {
                firmwareMachine.reset();
            } else {
                firmwareMachine->reset(); // CPU starts at the original reset vector
            }
        } else {
            firmwareMachine.reset();
        }
    }
    if (firmwareMachine != nullptr && resourceROM.isLoaded()) {
        // Extract the firmware-visible, descrambled IC4 code once on startup.
        // The FX catalog then resolves both IC51 banks and every block stream
        // before the real-time audio thread can access it.
        std::array<std::uint8_t, technox::TechnoX8032Machine::kCodeSpaceSize>
            firmwareCode{};
        for (std::size_t address = 0; address < firmwareCode.size(); ++address)
            firmwareCode[address] = firmwareMachine->readCode(
                static_cast<std::uint16_t>(address));

        if (resourceFXCatalog.load(
                resourceROM, firmwareCode.data(), firmwareCode.size())) {
            massEngine.setFXResourceCatalog(&resourceFXCatalog);
        }
    }

    initialisePartStates();
    initialiseCommonControlsFromBank();
    syncCurrentPerformanceToEngine();
}

bool QuasimidiTechnoXAudioProcessor::installVoiceBackend(
    std::unique_ptr<technox::IMASSVoiceBackend> backend) noexcept
{
    if (backend == nullptr)
        return false;

    // MASSAudioEngine clears the old backend and prepares the replacement
    // using the current host sample rate/block size before ownership changes.
    massEngine.setBackend(backend.get());
    voiceBackend = std::move(backend);
    return true;
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

void QuasimidiTechnoXAudioProcessor::initialiseCommonControlsFromBank() noexcept
{
    for (std::size_t performanceIndex = 0; performanceIndex < 50; ++performanceIndex) {
        const auto* performance = performanceBank.at(performanceIndex);
        if (performance == nullptr)
            continue;
        const auto& frame = performance->commonBlock.completeMessage;
        if (frame.size() != 74 || frame[0] != 0xF0 || frame[73] != 0xF7)
            continue;
        // Manual Common parameter map: FX1 type 0x2A, FX2 type 0x32,
        // Arpeggiator packed flags/resolution 0x3C, speed 0x3D.
        const int performanceMode = frame[8 + 0x01];
        const int fx1Type = frame[8 + 0x2A];
        const int fx2Type = frame[8 + 0x32];
        const int arpPacked = frame[8 + 0x3C];
        const int arpSpeed = frame[8 + 0x3D];
        if (performanceMode <= 3)
            commonValues[performanceIndex][0].store(performanceMode, std::memory_order_relaxed);
        if (fx1Type <= 127)
            commonValues[performanceIndex][1].store(
                std::min(fx1Type, technox::MASSCommonControlStore::kFXTypeMaximumIndex),
                std::memory_order_relaxed);
        if (fx2Type <= 127)
            commonValues[performanceIndex][2].store(
                std::min(fx2Type, technox::MASSCommonControlStore::kFXTypeMaximumIndex),
                std::memory_order_relaxed);
        commonValues[performanceIndex][3].store(arpPacked & 0x03, std::memory_order_relaxed);
        commonValues[performanceIndex][4].store(arpSpeed, std::memory_order_relaxed);
    }
}

void QuasimidiTechnoXAudioProcessor::syncCurrentPerformanceToEngine() noexcept
{
    const int performanceIndex = getCurrentProgram();
    if (performanceIndex < 0 || performanceIndex >= 50)
        return;
    massEngine.clearPerformanceImage();
    massEngine.clearCommonModulationDepths();
    const auto* selectedPerformance = performanceBank.at(static_cast<std::size_t>(performanceIndex));
    bool performanceImageValid = selectedPerformance != nullptr &&
        selectedPerformance->nameBlock.completeMessage.size() == 17 &&
        selectedPerformance->commonBlock.completeMessage.size() == 74;
    if (performanceImageValid) {
        const auto& nameFrame = selectedPerformance->nameBlock.completeMessage;
        for (int byte = 0; byte < 8; ++byte)
            performanceImageValid = massEngine.setPerformanceNameByte(byte, nameFrame[8 + static_cast<std::size_t>(byte)])
                && performanceImageValid;
        const auto& commonFrame = selectedPerformance->commonBlock.completeMessage;
        for (int byte = 0; byte < 65; ++byte)
            performanceImageValid = massEngine.setPerformanceCommonByte(byte, commonFrame[8 + static_cast<std::size_t>(byte)])
                && performanceImageValid;
        // Manual Common image bytes 0x09..0x28 store four rows of eight
        // modulation depths. Preserve their raw seven-bit values; DSP scaling
        // is still under reverse engineering.
        for (int source = 0; source < 4; ++source)
            for (int destination = 0; destination < 8; ++destination) {
                const int offset = 0x09 + source * 8 + destination;
                massEngine.setCommonModulationDepth(
                    source, destination, commonFrame[8 + static_cast<std::size_t>(offset)]);
            }
        for (int part = 0; part < 4; ++part) {
            const auto& frame = selectedPerformance->parts[static_cast<std::size_t>(part)].completeMessage;
            if (frame.size() != 33) { performanceImageValid = false; continue; }
            for (int byte = 0; byte < 24; ++byte)
                performanceImageValid = massEngine.setPerformancePartByte(part, byte,
                    frame[8 + static_cast<std::size_t>(byte)]) && performanceImageValid;
        }
    }
    massEngine.markPerformanceImageLoaded(performanceImageValid);

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
        technox::MASSCommonControl::performanceMode, technox::MASSCommonControl::fx1Type,
        technox::MASSCommonControl::fx2Type, technox::MASSCommonControl::arpResolution,
        technox::MASSCommonControl::arpSpeed
    };
    for (std::size_t i = 0; i < 5; ++i) {
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
    firmwareCycleTarget = firmwareMachine != nullptr
        ? static_cast<double>(firmwareMachine->elapsedMachineCycles()) : 0.0;
}
void QuasimidiTechnoXAudioProcessor::releaseResources() {}

void QuasimidiTechnoXAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    const double hostSampleRate = getSampleRate();
    const double firmwareBlockStartTarget = firmwareCycleTarget;
    const double firmwareCyclesPerSample =
        firmwareMachine != nullptr && hostSampleRate > 0.0
            ? static_cast<double>(firmwareMachine->machineCyclesPerSecond()) / hostSampleRate
            : 0.0;
    const int performanceIndex = getCurrentProgram();
    const int channelCount = buffer.getNumChannels();
    int renderedSamples = 0;

    // Render audio up to each MIDI event before applying it. Ignoring
    // MidiMessageMetadata::samplePosition made every note/controller change
    // take effect at the start of the block and caused timing jitter.
    const auto renderUntil = [this, &buffer, channelCount, &renderedSamples](int endSample)
    {
        const int end = juce::jlimit(renderedSamples, buffer.getNumSamples(), endSample);
        const int count = end - renderedSamples;
        if (count <= 0)
            return;

        std::array<float*, 2> channels{};
        for (int channel = 0; channel < channelCount; ++channel)
            channels[static_cast<std::size_t>(channel)] = buffer.getWritePointer(channel, renderedSamples);
        massEngine.render(channels.data(), channelCount, count);
        renderedSamples = end;
    };

    const auto clockFirmwareToSample =
        [this, firmwareBlockStartTarget, firmwareCyclesPerSample, numSamples](int samplePosition)
    {
        if (firmwareMachine == nullptr || firmwareMachine->cpuFaulted() ||
            firmwareCyclesPerSample <= 0.0)
            return;
        const int sample = juce::jlimit(0, numSamples, samplePosition);
        firmwareCycleTarget = firmwareBlockStartTarget +
            static_cast<double>(sample) * firmwareCyclesPerSample;
        const auto targetCycles = static_cast<std::uint64_t>(firmwareCycleTarget);
        const auto currentCycles = firmwareMachine->elapsedMachineCycles();
        if (targetCycles > currentCycles)
            firmwareMachine->runForMachineCycles(targetCycles - currentCycles);
    };

    const auto handleMessage = [this, performanceIndex](const juce::MidiMessage& message)
    {
        if (message.isController()) {
            if (message.getControllerNumber() == 124)
                setMidiRoutingMode(MidiRoutingMode::partChannels);
            else if (message.getControllerNumber() == 125)
                setMidiRoutingMode(MidiRoutingMode::performanceOmni);
        }
        const bool omniPerformance = getMidiRoutingMode() == MidiRoutingMode::performanceOmni;
        std::array<int, 4> targetSlots{};
        int targetCount = 0;
        if (omniPerformance) {
            for (int slot = 0; slot < 4; ++slot)
                if (isPerformancePartActive(slot))
                    targetSlots[static_cast<std::size_t>(targetCount++)] = slot;
        } else {
            const int slot = partSlotForMidiChannel(message.getChannel());
            if (slot >= 0)
                targetSlots[static_cast<std::size_t>(targetCount++)] = slot;
        }

        if (message.isNoteOn()) {
            for (int i = 0; i < targetCount; ++i) {
                const int slot = targetSlots[static_cast<std::size_t>(i)];
                massEngine.noteOn(static_cast<std::uint8_t>(12 + slot),
                                  static_cast<std::uint8_t>(message.getChannel()),
                                  static_cast<std::uint8_t>(message.getNoteNumber()),
                                  static_cast<std::uint8_t>(message.getVelocity()));
            }
        } else if (message.isNoteOff()) {
            massEngine.noteOff(static_cast<std::uint8_t>(message.getChannel()),
                               static_cast<std::uint8_t>(message.getNoteNumber()));
        } else if (message.isAllSoundOff()) {
            massEngine.allSoundsOff(message.getChannel());
        } else if (message.isAllNotesOff()) {
            massEngine.allNotesOff(message.getChannel());
        } else if (message.isPitchWheel()) {
            const int bend = message.getPitchWheelValue() - 8192;
            for (int i = 0; i < targetCount; ++i)
                massEngine.setPitchBend(12 + targetSlots[static_cast<std::size_t>(i)], bend);
        } else if (message.isChannelPressure()) {
            const int pressure = message.getChannelPressureValue();
            for (int i = 0; i < targetCount; ++i)
                massEngine.setAftertouch(12 + targetSlots[static_cast<std::size_t>(i)], pressure);
        } else if (message.isController()) {
            const int controller = message.getControllerNumber();
            const int value = message.getControllerValue();
            const int freeControllerNumber = massEngine.performanceImage().isLoaded()
                ? massEngine.performanceImage().commonByte(0x04) : -1;
            if (controller == 80)
                setCommonControlRaw(performanceIndex, technox::MASSCommonControl::fx1Type, value);
            else if (controller == 81)
                setCommonControlRaw(performanceIndex, technox::MASSCommonControl::fx2Type, value);
            else if (controller == 82)
                setCommonControlRaw(performanceIndex, technox::MASSCommonControl::arpResolution, value);
            else if (controller == 83)
                setCommonControlRaw(performanceIndex, technox::MASSCommonControl::arpSpeed, value);

            const auto bankSelectChange = !omniPerformance
                ? midiControlRouter.decodePartBankSelect(controller, value)
                : std::optional<technox::MidiControlRouter::BankSelectChange>{};
            const auto partChange = midiControlRouter.handleControlChange(message.getChannel(), controller, value);
            const auto drumChange = controller == 6
                ? midiControlRouter.decodeDrumNrpnDataEntry(message.getChannel(), value)
                : std::optional<technox::MidiControlRouter::DrumParameterChange>{};
            const auto rpnChange = controller == 6
                ? midiControlRouter.decodeRpnDataEntry(message.getChannel(), value)
                : std::optional<technox::MidiControlRouter::RpnParameterChange>{};
            const auto rpnLsbChange = controller == 38
                ? midiControlRouter.decodeRpnDataEntryLsb(message.getChannel(), value)
                : std::optional<technox::MidiControlRouter::RpnParameterChange>{};

            for (int i = 0; i < targetCount; ++i) {
                const int slot = targetSlots[static_cast<std::size_t>(i)];
                const int enginePart = 12 + slot;
                if (bankSelectChange.has_value())
                    setPartParameterRaw(performanceIndex, slot, technox::PartParameter::bankNumber,
                                         bankSelectChange->bankNumber);
                if (partChange.has_value())
                    setPartParameterRaw(performanceIndex, slot, partChange->parameter, partChange->rawValue);
                if (controller == 1)
                    massEngine.setModWheel(enginePart, value);
                else if (controller == 11)
                    massEngine.setExpression(enginePart, value);
                else if (controller == 64)
                    massEngine.setSustainPedal(enginePart, value >= 64);
                else if (controller == 65)
                    massEngine.setPortamento(enginePart, value >= 64);
                else if (controller == 67)
                    massEngine.setSoftPedal(enginePart, value >= 64);
                else if (controller == 121)
                    massEngine.resetPartControllers(enginePart);
                // Free Controller is an independently selected CC number and
                // may intentionally overlap a conventional controller.
                if (controller == freeControllerNumber && freeControllerNumber >= 0 && controller <= 97)
                    massEngine.setFreeController(enginePart, value);
                if (drumChange.has_value())
                    massEngine.setDrumParameter(enginePart, drumChange->keyNumber,
                                                drumChange->parameterIndex, drumChange->rawValue);
                if (rpnChange.has_value())
                    massEngine.setRpn(enginePart, rpnChange->parameterIndex, rpnChange->rawValue);
                if (rpnLsbChange.has_value())
                    massEngine.setRpnLsb(enginePart, rpnLsbChange->parameterIndex, rpnLsbChange->rawValue);
            }
        } else if (message.isProgramChange() && !omniPerformance) {
            for (int i = 0; i < targetCount; ++i) {
                const int slot = targetSlots[static_cast<std::size_t>(i)];
                setPartParameterRaw(performanceIndex, slot, technox::PartParameter::soundNumber,
                                    message.getProgramChangeNumber());
            }
        }
    };

    for (const auto metadata : midiMessages) {
        const int eventSample = juce::jlimit(renderedSamples, numSamples, metadata.samplePosition);
        renderUntil(eventSample);
        clockFirmwareToSample(eventSample);
        const auto& message = metadata.getMessage();
        if (firmwareMachine != nullptr && !firmwareMachine->cpuFaulted()) {
            const auto* rawBytes = message.getRawData();
            const auto rawSize = message.getRawDataSize();
            if (rawBytes != nullptr && rawSize > 0)
                firmwareMachine->scheduleAciaMidiBytes(
                    rawBytes, static_cast<std::size_t>(rawSize),
                    static_cast<std::uint64_t>(firmwareCycleTarget));
        }
        handleMessage(message);
    }
    renderUntil(numSamples);
    clockFirmwareToSample(numSamples);
    buffer.applyGain(getOutputGain());
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
    if (performanceIndex == getCurrentProgram()) {
        massEngine.setPartParameter(12 + partIndex, parameter, rawValue);
        massEngine.setPerformancePartByte(partIndex, static_cast<int>(parameterIndex), rawValue);
    }
    return true;
}

int QuasimidiTechnoXAudioProcessor::getCommonControlRaw(
    int performanceIndex, technox::MASSCommonControl control) const noexcept
{
    const auto index = static_cast<std::size_t>(control);
    if (performanceIndex < 0 || performanceIndex >= 50 || index >= 5)
        return -1;
    return commonValues[static_cast<std::size_t>(performanceIndex)][index].load(std::memory_order_acquire);
}

bool QuasimidiTechnoXAudioProcessor::setCommonControlRaw(
    int performanceIndex, technox::MASSCommonControl control, int rawValue) noexcept
{
    const auto index = static_cast<std::size_t>(control);
    constexpr int maxima[5] = {
        3, technox::MASSCommonControlStore::kFXTypeMaximumIndex,
        technox::MASSCommonControlStore::kFXTypeMaximumIndex, 3, 127
    };
    if (performanceIndex < 0 || performanceIndex >= 50 || index >= 5 || rawValue < 0)
        return false;
    if (control == technox::MASSCommonControl::fx1Type ||
        control == technox::MASSCommonControl::fx2Type)
        rawValue = std::min(rawValue, technox::MASSCommonControlStore::kFXTypeMaximumIndex);
    else if (rawValue > maxima[index])
        return false;
    commonValues[static_cast<std::size_t>(performanceIndex)][index].store(rawValue, std::memory_order_release);
    if (performanceIndex == getCurrentProgram()) {
        massEngine.setCommonControl(control, rawValue);
        constexpr std::array<int, 5> payloadOffsets{{0x01, 0x2A, 0x32, 0x3C, 0x3D}};
        massEngine.setPerformanceCommonByte(payloadOffsets[index], rawValue);
    }
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

MidiRoutingMode QuasimidiTechnoXAudioProcessor::getMidiRoutingMode() const noexcept
{
    return midiRoutingMode.load(std::memory_order_acquire) == 1
        ? MidiRoutingMode::partChannels : MidiRoutingMode::performanceOmni;
}

void QuasimidiTechnoXAudioProcessor::setMidiRoutingMode(MidiRoutingMode mode) noexcept
{
    midiRoutingMode.store(static_cast<int>(mode), std::memory_order_release);
}

bool QuasimidiTechnoXAudioProcessor::isPerformancePartActive(int partIndex) const noexcept
{
    if (partIndex < 0 || partIndex >= 4)
        return false;
    const int performanceIndex = static_cast<int>(performanceBank.selectedIndex());
    const int mode = getCommonControlRaw(performanceIndex, technox::MASSCommonControl::performanceMode);
    if (mode < 0 || mode > 3 || partIndex > mode)
        return false;
    const int trackMode = getPartParameterRaw(performanceIndex, partIndex, technox::PartParameter::trackMode);
    return technox::isPerformanceLayerActive(mode, partIndex, trackMode);
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
    const int performance = static_cast<int>(performanceBank.selectedIndex());
    const int bank = getPartParameterRaw(performance, partIndex, technox::PartParameter::bankNumber);
    const int sound = getPartParameterRaw(performance, partIndex, technox::PartParameter::soundNumber);
    return soundCatalog.nameFor(bank, sound);
}

juce::String QuasimidiTechnoXAudioProcessor::getCurrentPartResourceSummary(int partIndex) const
{
    if (partIndex < 0 || partIndex >= 4)
        return "Part unavailable";

    const int performance = static_cast<int>(performanceBank.selectedIndex());
    const int bank = getPartParameterRaw(performance, partIndex, technox::PartParameter::bankNumber);
    const int sound = getPartParameterRaw(performance, partIndex, technox::PartParameter::soundNumber);
    const auto index = static_cast<std::size_t>(partIndex);
    if (bank != resolvedResourceBanks[index] || sound != resolvedResourcePrograms[index]) {
        technox::MASSResolvedProgramResource resolved{};
        resolvedResourceValid[index] = technox::MASSResourceProgramResolver::resolve(
            resourceROM, bank, sound, resolved);
        resolvedResourceBanks[index] = bank;
        resolvedResourcePrograms[index] = sound;
        if (resolvedResourceValid[index])
            resolvedResources[index] = std::move(resolved);
    }

    if (!resolvedResourceValid[index])
        return resourceROM.isLoaded() ? "IC51 resource unavailable" : "IC51 ROM not embedded";

    const auto& resource = resolvedResources[index];
    return juce::String::formatted("IC51 %04X  STREAM %04X  %d CMD",
        static_cast<int>(resource.descriptorAddress),
        static_cast<int>(resource.descriptor.commandStreamPointer),
        static_cast<int>(resource.stream.commands.size()));
}

juce::AudioProcessorEditor* QuasimidiTechnoXAudioProcessor::createEditor()
{
    return new QuasimidiTechnoXAudioProcessorEditor(*this);
}

void QuasimidiTechnoXAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream(destData, false);
    stream.writeInt(kStateMagicV4);
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
    stream.writeByte(static_cast<char>(static_cast<int>(getMidiRoutingMode())));
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
    if (magic != kStateMagicV2 && magic != kStateMagicV3 && magic != kStateMagicV4)
        return;
    const int requiredBytes = magic == kStateMagicV2 ? kPartStateBytesV2
        : (magic == kStateMagicV3 ? kPartStateBytesV3 : kPartStateBytesV4);
    if (sizeInBytes < 8 + requiredBytes)
        return;

    for (std::size_t performance = 0; performance < partValues.size(); ++performance)
        for (std::size_t part = 0; part < partValues[performance].size(); ++part)
            for (std::size_t parameter = 0; parameter < partValues[performance][part].size(); ++parameter) {
                const int value = static_cast<unsigned char>(stream.readByte());
                const auto maximum = technox::kPartParameterDescriptors[parameter].maximum;
                partValues[performance][part][parameter].store(
                    juce::jlimit(0, static_cast<int>(maximum), value), std::memory_order_release);
            }

    if (magic == kStateMagicV4) {
        constexpr int maxima[5] = {
            3, technox::MASSCommonControlStore::kFXTypeMaximumIndex,
            technox::MASSCommonControlStore::kFXTypeMaximumIndex, 3, 127
        };
        for (std::size_t performance = 0; performance < commonValues.size(); ++performance)
            for (std::size_t control = 0; control < 5; ++control) {
                const int value = static_cast<unsigned char>(stream.readByte());
                commonValues[performance][control].store(
                    value == 0xff ? -1 : juce::jlimit(0, maxima[control], value),
                    std::memory_order_release);
            }
    } else if (magic == kStateMagicV3) {
        initialiseCommonControlsFromBank();
        constexpr int oldMaxima[4] = {
            technox::MASSCommonControlStore::kFXTypeMaximumIndex,
            technox::MASSCommonControlStore::kFXTypeMaximumIndex, 3, 127
        };
        for (std::size_t performance = 0; performance < commonValues.size(); ++performance)
            for (std::size_t control = 0; control < 4; ++control) {
                const int value = static_cast<unsigned char>(stream.readByte());
                commonValues[performance][control + 1].store(
                    value == 0xff ? -1 : juce::jlimit(0, oldMaxima[control], value),
                    std::memory_order_release);
            }
    } else {
        initialiseCommonControlsFromBank();
    }

    for (auto& channel : partMidiChannels) {
        const int restoredChannel = static_cast<unsigned char>(stream.readByte());
        if (restoredChannel >= 1 && restoredChannel <= 16)
            channel.store(restoredChannel, std::memory_order_release);
    }
    if (magic == kStateMagicV4) {
        const int mode = static_cast<unsigned char>(stream.readByte());
        midiRoutingMode.store(mode == 1 ? 1 : 0, std::memory_order_release);
    } else {
        midiRoutingMode.store(static_cast<int>(MidiRoutingMode::performanceOmni), std::memory_order_release);
    }
    setCurrentProgram(program);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QuasimidiTechnoXAudioProcessor();
}
