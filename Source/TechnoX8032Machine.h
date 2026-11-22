#pragma once

// First-pass Intel 8032 machine bus for the Quasimidi TechnoX v1.05 firmware.
// The IC4 transform and known MAME I/O windows are reproduced. The C000-FFFF
// read/write backing store follows the firmware self-test; physical RAM identity
// is not claimed. Undocumented command-port reads remain open bus, not guessed.

#include "MCS51Core.h"
#include "MCS51Timers.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace technox {

// Undocumented external register window observed at 0x8000-0x8003.
// Register meanings are intentionally left unnamed until the hardware is identified.
enum class ExternalAudioRegister : std::uint8_t {
    register0 = 0, register1 = 1, register2 = 2, register3 = 3
};

constexpr std::uint16_t externalAudioRegisterAddress(ExternalAudioRegister reg) noexcept {
    return static_cast<std::uint16_t>(0x8000u + static_cast<std::uint8_t>(reg));
}

static_assert(externalAudioRegisterAddress(ExternalAudioRegister::register3) == 0x8003u,
              "Observed external register window ends at offset 3");

class TechnoX8032Machine final : public MCS51Bus {
public:
    struct SoundCommandPacket {
        // Snapshot of sequential writes to the undocumented 0x8000-0x8003
        // external register window. The register meanings are not established.
        std::array<std::uint8_t, 4> bytes{};
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint64_t machineCycle = 0;
    };
    struct soundHostReadEvent {
        std::uint8_t registerIndex = 0;
        std::uint8_t value = 0xff;
        std::uint16_t instructionPC = 0;
        std::uint64_t machineCycle = 0;
    };
    struct soundHostWriteEvent {
        std::uint8_t registerIndex = 0;
        std::uint8_t value = 0;
        std::uint16_t instructionPC = 0;
        std::uint64_t machineCycle = 0;
    };
    struct SoundHostBurstWordEvent {
        std::uint16_t value = 0;
        std::uint8_t lowByte = 0;
        std::uint8_t highByte = 0;
        std::uint16_t lowInstructionPC = 0;
        std::uint16_t highInstructionPC = 0;
        std::uint64_t machineCycle = 0;
    };
    struct PortWriteEvent {
        std::uint8_t port = 0;
        std::uint8_t value = 0xff;
        std::uint64_t machineCycle = 0;
    };
    struct PortReadEvent {
        std::uint8_t port = 0;
        std::uint8_t value = 0xff;
        std::uint64_t machineCycle = 0;
    };
    struct IC51ReadEvent {
        std::uint16_t address = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint8_t bank = 0;
        std::uint8_t value = 0xff;
        std::uint64_t machineCycle = 0;
    };
    struct SoundMemoryWriteEvent {
        std::uint16_t address = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint8_t value = 0;
        std::uint8_t mode = 0;
        std::uint64_t machineCycle = 0;
    };
    struct SoundMemoryReadEvent {
        std::uint16_t address = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint8_t value = 0xff;
        std::uint8_t mode = 0;
        std::uint64_t machineCycle = 0;
    };
    struct IC51BusProbeEvent {
        std::uint16_t address = 0;
        std::uint16_t instructionPC = 0;
        std::uint8_t p3 = 0xff;
        std::uint8_t valueOnCurrentMap = 0xff;
        bool selected = false;
        std::uint8_t bank = 0;
        std::uint64_t machineCycle = 0;
    };
    struct XdataWatchEvent {
        std::uint16_t address = 0;
        std::uint8_t value = 0xff;
        bool write = false;
        std::uint64_t machineCycle = 0;
    };
    struct UnmappedBusEvent {
        std::uint16_t address = 0;
        std::uint8_t value = 0xff;
        bool write = false;
        std::uint64_t machineCycle = 0;
    };
    struct ExternalWindowEvent {
        std::uint16_t address = 0;
        std::uint8_t value = 0xff;
        bool write = false;
        std::uint64_t machineCycle = 0;
    };
    // MAME's skeleton driver currently assumes 12 MHz but explicitly marks
    // the actual oscillator as unknown. Keep the rate injectable.
    static constexpr std::uint32_t kAssumedOscillatorHz = 12'000'000u;
    explicit TechnoX8032Machine(
        std::uint32_t oscillatorHz = kAssumedOscillatorHz) noexcept
        : oscillatorHz_(oscillatorHz) {}

    std::uint32_t oscillatorHz() const noexcept { return oscillatorHz_; }
    std::uint32_t machineCyclesPerSecond() const noexcept {
        return oscillatorHz_ / 12u; // classic 12T 8032 core
    }

    // IC4 routine AEDD applies this affine map to R0 before the
    // external sound-memory MOVX write in AF05 (ROM table at 0xADDD).
    static constexpr std::uint8_t mapSoundMemoryLowAddress(
        std::uint8_t logical) noexcept {
        std::uint8_t physical = 0x04u;
        if ((logical & 0x01u) != 0) physical ^= 0x21u;
        if ((logical & 0x02u) != 0) physical ^= 0x12u;
        if ((logical & 0x04u) != 0) physical ^= 0x80u;
        if ((logical & 0x08u) != 0) physical ^= 0x20u;
        if ((logical & 0x10u) != 0) physical ^= 0x40u;
        if ((logical & 0x20u) != 0) physical ^= 0x08u;
        if ((logical & 0x40u) != 0) physical ^= 0x04u;
        if ((logical & 0x80u) != 0) physical ^= 0x10u;
        return physical;
    }

    static constexpr std::uint8_t unmapSoundMemoryLowAddress(
        std::uint8_t physical) noexcept {
        const auto value = static_cast<std::uint8_t>(physical ^ 0x04u);
        std::uint8_t logical = 0;
        if ((value & 0x01u) != 0) logical ^= 0x09u;
        if ((value & 0x02u) != 0) logical ^= 0x82u;
        if ((value & 0x04u) != 0) logical ^= 0x40u;
        if ((value & 0x08u) != 0) logical ^= 0x20u;
        if ((value & 0x10u) != 0) logical ^= 0x80u;
        if ((value & 0x20u) != 0) logical ^= 0x08u;
        if ((value & 0x40u) != 0) logical ^= 0x10u;
        if ((value & 0x80u) != 0) logical ^= 0x04u;
        return logical;
    }

    static constexpr std::size_t kIC4RomSize = 0x20000;
    static constexpr std::size_t kIC51RomSize = 0x20000;
    static constexpr std::size_t kCodeSpaceSize = 0x10000;

    bool loadIC4Rom(const std::uint8_t* raw, std::size_t size) noexcept {
        if (raw == nullptr || size != kIC4RomSize)
            return false;

        for (std::size_t base = 0; base < kIC4RomSize; base += 0x100) {
            std::array<std::uint8_t, 0x100> source{};
            std::copy_n(raw + base, source.size(), source.begin());
            for (unsigned offset = 0; offset < 0x100; ++offset) {
                const auto sourceOffset = descrambledOffset(static_cast<std::uint8_t>(offset));
                rom_[base + offset] = source[sourceOffset];
            }
        }
        std::copy_n(rom_.begin(), code_.size(), code_.begin());
        romLoaded_ = true;
        return true;
    }

    bool loadIC51Rom(const std::uint8_t* image, std::size_t size) noexcept {
        if (image == nullptr || size != kIC51RomSize)
            return false;
        std::copy_n(image, size, ic51Rom_.begin());
        ic51RomLoaded_ = true;
        return true;
    }

    bool ic51RomLoaded() const noexcept { return ic51RomLoaded_; }
    std::uint64_t ic51ReadCount() const noexcept { return ic51ReadCount_; }
    std::uint64_t ic51ReadsByBank(std::size_t bank) const noexcept {
        return bank < ic51ReadsByBank_.size() ? ic51ReadsByBank_[bank] : 0;
    }
    std::uint16_t lastIC51ReadAddress() const noexcept { return lastIC51ReadAddress_; }
    std::size_t ic51ReadTraceCount() const noexcept { return ic51ReadTraceCount_; }
    const IC51ReadEvent& ic51ReadTraceAt(std::size_t index) const noexcept {
        return ic51ReadTrace_[index % ic51ReadTrace_.size()];
    }
    std::uint8_t ic51RomByteForTesting(std::size_t bank, std::uint16_t address) const noexcept {
        return bank < 2u ? ic51Rom_[bank * 0x10000u + address] : 0xffu;
    }
    std::size_t xdataWatchTraceCount() const noexcept { return xdataWatchTraceCount_; }
    const XdataWatchEvent& xdataWatchTraceAt(std::size_t index) const noexcept {
        return xdataWatchTrace_[index % xdataWatchTrace_.size()];
    }
    std::size_t unmappedBusTraceCount() const noexcept { return unmappedBusTraceCount_; }
    const UnmappedBusEvent& unmappedBusTraceAt(std::size_t index) const noexcept {
        return unmappedBusTrace_[index % unmappedBusTrace_.size()];
    }
    std::uint64_t unmappedReadCountAt7xxx(std::uint16_t address) const noexcept {
        return address >= 0x7000u && address <= 0x7fffu
            ? unmappedReadCounts7xxx_[address - 0x7000u] : 0;
    }
    std::uint64_t unmappedWriteCountAt7xxx(std::uint16_t address) const noexcept {
        return address >= 0x7000u && address <= 0x7fffu
            ? unmappedWriteCounts7xxx_[address - 0x7000u] : 0;
    }

    void reset() noexcept {
        cpu_.reset();
        timers_.reset();
        internal_.fill(0);
        upperIram_.fill(0);
        xdata_.fill(0);
        portLatch_.fill(0xff);
        portPins_.fill(0xff);
        externalPortInputMask_.fill(0);
        externalPortInputValue_.fill(0xff);
        commandPort_.fill(0);
        soundHostCommandLatch_ = 0;
        soundHostWords_.fill(0);
        for (auto& bytes : soundHostData_) bytes.fill(0);
        soundCommandPacketCount_ = 0;
        soundCommandTrace_.fill(SoundCommandPacket{});
        soundHostReadCount_ = 0;
        soundHostReadTrace_.fill(soundHostReadEvent{});
        soundHostWriteCount_ = 0;
        soundHostWriteTrace_.fill(soundHostWriteEvent{});
        soundHostTransactionActive_ = false;
        soundHostExpectedRegister_ = 0;
        soundHostTransactionBytes_.fill(0);
        soundHostBurstWordCount_ = 0;
        soundHostBurstWordTrace_.fill(SoundHostBurstWordEvent{});
        soundHostBurstLowPending_ = false;
        soundHostBurstLowByte_ = 0;
        soundHostBurstLowInstructionPC_ = 0;
        portWriteTrace_.fill(PortWriteEvent{});
        portWriteCount_ = 0;
        portReadTrace_.fill(PortReadEvent{});
        portReadCount_ = 0;
        watchedCallCounts_.fill(0);
        ic51ReadTrace_.fill(IC51ReadEvent{});
        ic51ReadTraceCount_ = 0;
        xdataWatchTrace_.fill(XdataWatchEvent{});
        xdataWatchTraceCount_ = 0;
        unmappedBusTrace_.fill(UnmappedBusEvent{});
        unmappedBusTraceCount_ = 0;
        unmappedReadCounts7xxx_.fill(0);
        unmappedWriteCounts7xxx_.fill(0);
        externalWindowTrace_.fill(ExternalWindowEvent{});
        externalWindowTraceCount_ = 0;
        aciaControl_ = 0;
        aciaStatus_ = 0x02; // TDRE
        aciaReceive_ = 0;
        aciaReceiveReadCount_ = 0;
        aciaValidReceiveReadCount_ = 0;
        lastAciaReceiveRead_ = 0;
        lastValidAciaReceiveRead_ = 0;
        aciaTransmit_ = 0;
        aciaTransmitPending_ = false;
        aciaTransmitterBusy_ = false;
        aciaTxReadyCycle_ = 0;
        lcdStatus_ = 0;
        lcdData_ = 0;
        lcdInstruction_ = 0;
        elapsedMachineCycles_ = 0;
        soundDeviceResetReleased_ = true;
        soundDeviceWrite_ = 0;
        soundDeviceResponse_ = 0xff; // external sound-device response is unknown/open bus
        cpuFaulted_ = false;
        scheduledAciaHead_ = 0;
        scheduledAciaTail_ = 0;
        scheduledAciaCount_ = 0;
        nextAciaReceiveCycle_ = 0;
        soundMemoryReadCount_ = 0;
        soundMemoryReadTraceCount_ = 0;
        soundMemoryWriteCount_ = 0;
        soundMemoryWriteTraceCount_ = 0;
        ic51BusProbeTraceCount_ = 0;
        soundBusModeWriteCount_ = 0;
        p35TransitionCount_ = 0;
        lastSoundMemoryWriteAddress_ = 0;
        lastSoundMemoryWriteValue_ = 0;
        soundMemoryReadsByMode_.fill(0);
        soundMemoryWritesByMode_.fill(0);
        ic51ReadCount_ = 0;
        ic51ReadsByBank_.fill(0);
        lastIC51ReadAddress_ = 0;
        internal_[0x81] = 0x07; // SP reset value
    }

    bool romLoaded() const noexcept { return romLoaded_; }
    const MCS51Core& cpu() const noexcept { return cpu_; }
    MCS51Core& cpuForTesting() noexcept { return cpu_; }
    std::uint64_t elapsedMachineCycles() const noexcept { return elapsedMachineCycles_; }

    std::uint64_t watchedCallCount(std::uint16_t target) const noexcept {
        switch (target) {
            case 0x57a8: return watchedCallCounts_[0];
            case 0x5671: return watchedCallCounts_[1];
            case 0x4256: return watchedCallCounts_[2];
            case 0x1494: return watchedCallCounts_[3];
            case 0x0db7: return watchedCallCounts_[4];
            case 0xac00: return watchedCallCounts_[5];
            case 0xac88: return watchedCallCounts_[6];
            case 0xb34d: return watchedCallCounts_[7];
            default: return 0;
        }
    }

    MCS51StepResult step() noexcept {
        const auto pc = cpu_.registers().pc;
        if (code_[pc] == 0x12u) {
            const auto target = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(code_[static_cast<std::uint16_t>(pc + 1u)]) << 8) |
                code_[static_cast<std::uint16_t>(pc + 2u)]);
            switch (target) {
                case 0x57a8: ++watchedCallCounts_[0]; break;
                case 0x5671: ++watchedCallCounts_[1]; break;
                case 0x4256: ++watchedCallCounts_[2]; break;
                case 0x1494: ++watchedCallCounts_[3]; break;
                case 0x0db7: ++watchedCallCounts_[4]; break;
                case 0xac00: ++watchedCallCounts_[5]; break;
                case 0xac88: ++watchedCallCounts_[6]; break;
                case 0xb34d: ++watchedCallCounts_[7]; break;
                default: break;
            }
        }
        return cpu_.step(*this);
    }

    std::size_t runInstructions(std::size_t maximumInstructions) noexcept {
        std::size_t executed = 0;
        while (executed < maximumInstructions) {
            const auto result = step();
            if (result != MCS51StepResult::executed) {
                if (result == MCS51StepResult::unsupportedOpcode)
                    cpuFaulted_ = true;
                break;
            }
            ++executed;
        }
        return executed;
    }

    std::size_t runForMachineCycles(std::uint64_t cycleBudget) noexcept {
        const auto target = elapsedMachineCycles_ + cycleBudget;
        std::size_t executed = 0;
        while (elapsedMachineCycles_ < target) {
            const auto result = step();
            if (result != MCS51StepResult::executed) {
                if (result == MCS51StepResult::unsupportedOpcode)
                    cpuFaulted_ = true;
                break;
            }
            ++executed;
        }
        return executed;
    }

    bool cpuFaulted() const noexcept { return cpuFaulted_; }

    std::size_t runUntilProgramCounter(std::uint16_t target,
                                       std::size_t maximumInstructions) noexcept {
        std::size_t executed = 0;
        while (executed < maximumInstructions && cpu_.registers().pc != target) {
            const auto result = step();
            if (result != MCS51StepResult::executed) {
                if (result == MCS51StepResult::unsupportedOpcode)
                    cpuFaulted_ = true;
                break;
            }
            ++executed;
        }
        return executed;
    }

    std::uint8_t readCode(std::uint16_t address) const noexcept override {
        return code_[address];
    }

    std::uint8_t readInternal(std::uint8_t address) const noexcept override {
        const int port = portIndex(address);
        if (port < 0) return internal_[address];
        const auto index = static_cast<std::size_t>(port);
        const auto mask = externalPortInputMask_[index];
        const auto value = static_cast<std::uint8_t>(
            (portPins_[index] & static_cast<std::uint8_t>(~mask)) |
            (externalPortInputValue_[index] & mask));
        auto& event = portReadTrace_[portReadCount_ % portReadTrace_.size()];
        event.port = static_cast<std::uint8_t>(port);
        event.value = value;
        event.machineCycle = elapsedMachineCycles_;
        ++portReadCount_;
        return value;
    }

    void setExternalPortInputs(std::uint8_t portAddress, std::uint8_t mask,
                               std::uint8_t value) noexcept {
        const int port = portIndex(portAddress);
        if (port < 0) return;
        const auto index = static_cast<std::size_t>(port);
        externalPortInputMask_[index] = mask;
        externalPortInputValue_[index] = value;
    }

    void writeInternal(std::uint8_t address, std::uint8_t value) noexcept override {
        const int port = portIndex(address);
        if (port >= 0) {
            const auto index = static_cast<std::size_t>(port);
            const auto previousPins = portPins_[index];
            portLatch_[index] = value;
            portPins_[index] = value;
            auto& portEvent = portWriteTrace_[portWriteCount_ % portWriteTrace_.size()];
            portEvent.port = static_cast<std::uint8_t>(port);
            portEvent.value = value;
            portEvent.machineCycle = elapsedMachineCycles_;
            ++portWriteCount_;
            if (port == 3) {
                if (((previousPins ^ value) & 0x20u) != 0)
                    ++p35TransitionCount_;
                const bool wasReleased = (previousPins & 0x04u) != 0;
                const bool nowReleased = (value & 0x04u) != 0;
                if (wasReleased && !nowReleased)
                    soundDeviceResetReleased_ = false;
                else if (!wasReleased && nowReleased)
                    soundDeviceResetReleased_ = true;
            }
        } else {
            internal_[address] = value;
            if (address == 0x8eu)
                ++soundBusModeWriteCount_;
        }
    }

    std::uint8_t readPortLatch(std::uint8_t address) const noexcept override {
        const int port = portIndex(address);
        return port >= 0 ? portLatch_[static_cast<std::size_t>(port)] : readInternal(address);
    }

    std::uint8_t readIndirect(std::uint8_t address) const noexcept override {
        return address < 0x80u ? internal_[address] : upperIram_[address - 0x80u];
    }

    void writeIndirect(std::uint8_t address, std::uint8_t value) noexcept override {
        if (address < 0x80u) internal_[address] = value;
        else upperIram_[address - 0x80u] = value;
    }

    std::uint8_t readExternal(std::uint16_t address) noexcept override {
        const auto instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
        const bool ic51Selected = isIC51Selected();
        if (instructionPC >= 0xaf3cu && instructionPC <= 0xaf63u) {
            auto& probe = ic51BusProbeTrace_[
                ic51BusProbeTraceCount_ % ic51BusProbeTrace_.size()];
            probe.address = address;
            probe.instructionPC = instructionPC;
            probe.p3 = portPins_[3];
            probe.selected = ic51Selected;
            probe.bank = static_cast<std::uint8_t>((portPins_[3] & 0x20u) != 0 ? 1u : 0u);
            probe.valueOnCurrentMap = ic51Selected
                ? ic51Rom_[static_cast<std::size_t>(probe.bank) * 0x10000u + address]
                : (isXdataRam(address) ? xdata_[address] : 0xffu);
            probe.machineCycle = elapsedMachineCycles_;
            ++ic51BusProbeTraceCount_;
        }
        const bool ramMode = (internal_[0x8e] & 0x03u) == 0x02u;
        const bool peripheralAddress = isDedicatedPeripheralAddress(address);
        if (ic51Selected && !peripheralAddress && !(ramMode && isXdataRam(address))) {
            const auto bank = (portPins_[3] & 0x20u) != 0 ? 1u : 0u;
            ++ic51ReadCount_;
            ++ic51ReadsByBank_[bank];
            lastIC51ReadAddress_ = address;
            const auto value = ic51Rom_[bank * 0x10000u + address];
            auto& event = ic51ReadTrace_[ic51ReadTraceCount_ % ic51ReadTrace_.size()];
            event.address = address;
            event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
            const auto stackPointer = cpu_.registers().sp;
            const auto returnHigh = readIndirect(stackPointer);
            const auto returnLow = readIndirect(static_cast<std::uint8_t>(stackPointer - 1u));
            event.returnPC = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(returnHigh) << 8u) | returnLow);
            event.bank = static_cast<std::uint8_t>(bank);
            event.value = value;
            event.machineCycle = elapsedMachineCycles_;
            ++ic51ReadTraceCount_;
            return value;
        }
        if (address >= 0x8000u && address <= 0x8003u) {
            const auto index = static_cast<std::size_t>(address - 0x8000u);
            // Firmware helper 0x0DCE selects an index via register 0, then
            // reads response bytes from registers 1, 2 and 3 into R2, R3, R4.
            // Keep all three response bytes per selected index; the old model
            // incorrectly echoed only a 16-bit pair and lost register 1.
            const std::uint8_t value = index == 0
                ? commandPort_[index]
                : soundHostData_[soundHostCommandLatch_][index - 1u];
            auto& event = soundHostReadTrace_[
                soundHostReadCount_ % soundHostReadTrace_.size()];
            event.registerIndex = static_cast<std::uint8_t>(index);
            event.value = value;
            event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
            event.machineCycle = elapsedMachineCycles_;
            ++soundHostReadCount_;
            return value;
        }
        if (address == 0xff10u && (internal_[0x8e] & 0x03u) != 0x02u)
            return soundDeviceResponse_; // status port outside RAM mode
        if (isXdataRam(address)) {
            if (address >= 0xc000u) {
                ++soundMemoryReadCount_;
                const auto mode = static_cast<std::size_t>(internal_[0x8e] & 0x03u);
                ++soundMemoryReadsByMode_[mode];
                auto& event = soundMemoryReadTrace_[
                    soundMemoryReadTraceCount_ % soundMemoryReadTrace_.size()];
                event.address = address;
                event.instructionPC = instructionPC;
                const auto stackPointer = cpu_.registers().sp;
                const auto returnHigh = readIndirect(stackPointer);
                const auto returnLow = readIndirect(static_cast<std::uint8_t>(stackPointer - 1u));
                event.returnPC = static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(returnHigh) << 8u) | returnLow);
                event.value = xdata_[address];
                event.mode = static_cast<std::uint8_t>(mode);
                event.machineCycle = elapsedMachineCycles_;
                ++soundMemoryReadTraceCount_;
            }
            const auto value = xdata_[address];
            if (address >= 0x7bd4u && address <= 0x7bd7u) {
                auto& event = xdataWatchTrace_[xdataWatchTraceCount_ % xdataWatchTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = false;
                event.machineCycle = elapsedMachineCycles_;
                ++xdataWatchTraceCount_;
            }
            return value;
        }

        if (testExternalReadOverrideEnabled_ && address == testExternalReadOverrideAddress_) {
            const auto value = testExternalReadOverrideValue_;
            if (address >= 0x7bd0u && address <= 0x7bd7u) {
                auto& event = externalWindowTrace_[externalWindowTraceCount_ % externalWindowTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = false;
                event.machineCycle = elapsedMachineCycles_;
                ++externalWindowTraceCount_;
            }
            return value;
        }
        if (testExternalRam7000Enabled_ && address >= 0x7000u && address <= 0x7fffu) {
            const auto value = testExternalRam7000_[address - 0x7000u];
            if (address >= 0x7bd0u && address <= 0x7bd7u) {
                auto& event = externalWindowTrace_[externalWindowTraceCount_ % externalWindowTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = false;
                event.machineCycle = elapsedMachineCycles_;
                ++externalWindowTraceCount_;
            }
            return value;
        }

        switch (address) {
            case 0xa002: return (aciaControl_ & 0x03u) == 0x03u
                ? static_cast<std::uint8_t>(aciaStatus_ & ~0x02u) : aciaStatus_;
            case 0xa003: {
                const auto value = aciaReceive_;
                ++aciaReceiveReadCount_;
                lastAciaReceiveRead_ = value;
                if ((aciaStatus_ & 0x01u) != 0) {
                    ++aciaValidReceiveReadCount_;
                    lastValidAciaReceiveRead_ = value;
                }
                aciaStatus_ = static_cast<std::uint8_t>(aciaStatus_ & ~0x21u); // clear RDRF/OVRN
                refreshAciaInterrupt();
                return value;
            }
            case 0xb002: return lcdStatus_;
            case 0xb003: return lcdData_;
            default:
                if (address >= 0x3900u && address <= 0xbfffu) {
                    auto& event = unmappedBusTrace_[unmappedBusTraceCount_ % unmappedBusTrace_.size()];
                    event.address = address;
                    event.value = 0xff;
                    event.write = false;
                    event.machineCycle = elapsedMachineCycles_;
                    ++unmappedBusTraceCount_;
                    if (address >= 0x7000u && address <= 0x7fffu)
                        ++unmappedReadCounts7xxx_[address - 0x7000u];
                }
                return 0xff; // open bus / unmodeled hardware
        }
    }

    void writeExternal(std::uint16_t address, std::uint8_t value) noexcept override {
        const bool ramMode = (internal_[0x8e] & 0x03u) == 0x02u;
        if (isIC51Selected() && !isDedicatedPeripheralAddress(address) &&
            !(ramMode && isXdataRam(address)))
            return; // IC51 is read-only; dedicated I/O and RAM remain writable.
        if (address == 0xff10u && (internal_[0x8e] & 0x03u) != 0x02u) {
            soundDeviceWrite_ = value; // status-port traffic outside RAM mode
            return;
        }
        if (isXdataRam(address)) {
            xdata_[address] = value;
            if (address >= 0x7bd4u && address <= 0x7bd7u) {
                auto& event = xdataWatchTrace_[xdataWatchTraceCount_ % xdataWatchTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = true;
                event.machineCycle = elapsedMachineCycles_;
                ++xdataWatchTraceCount_;
            }
            if (address >= 0xc000u) {
                ++soundMemoryWriteCount_;
                const auto mode = static_cast<std::size_t>(internal_[0x8e] & 0x03u);
                ++soundMemoryWritesByMode_[mode];
                lastSoundMemoryWriteAddress_ = address;
                lastSoundMemoryWriteValue_ = value;
                auto& event = soundMemoryWriteTrace_[
                    soundMemoryWriteTraceCount_ % soundMemoryWriteTrace_.size()];
                event.address = address;
                event.value = value;
                event.mode = static_cast<std::uint8_t>(mode);
                event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
                auto stackPointer = cpu_.registers().sp;
                // AF05 saves R0 before the sound-RAM MOVX at AF10; skip that
                // one stack byte to recover the caller return address.
                if (event.instructionPC == 0xaf10u)
                    stackPointer = static_cast<std::uint8_t>(stackPointer - 1u);
                const auto returnHigh = readIndirect(stackPointer);
                const auto returnLow = readIndirect(static_cast<std::uint8_t>(stackPointer - 1u));
                event.returnPC = static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(returnHigh) << 8u) | returnLow);
                event.machineCycle = elapsedMachineCycles_;
                ++soundMemoryWriteTraceCount_;
            }
            return;
        }

        if (address >= 0x8000u && address <= 0x8003u) {
            const auto index = static_cast<std::size_t>(address - 0x8000u);
            const auto instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
            commandPort_[index] = value;
            if (index == static_cast<std::size_t>(ExternalAudioRegister::register0)) {
                // IC4 writes this register before the following register accesses.
                // Preserve the observed ordering without assigning semantics.
                soundHostCommandLatch_ = value;
            } else {
                // Registers 1..3 are the three-byte payload at the selected
                // device index, matching the firmware's R2/R3/R4 readback.
                // 0x40/0xC0 writes to register 3 are handshake strobes from
                // helpers 0x0DCE/0x0E21, not payload byte R4.
                const bool handshakeStrobe =
                    index == static_cast<std::size_t>(ExternalAudioRegister::register3) &&
                    (value == 0x40u || value == 0xc0u);
                if (!handshakeStrobe)
                    soundHostData_[soundHostCommandLatch_][index - 1u] = value;
                if (index == static_cast<std::size_t>(ExternalAudioRegister::register2)) {
                    auto& word = soundHostWords_[soundHostCommandLatch_];
                    word = static_cast<std::uint16_t>((word & 0xff00u) | value);
                } else if (index == static_cast<std::size_t>(ExternalAudioRegister::register3) &&
                           !handshakeStrobe) {
                    auto& word = soundHostWords_[soundHostCommandLatch_];
                    word = static_cast<std::uint16_t>((word & 0x00ffu) |
                        (static_cast<std::uint16_t>(value) << 8u));
                }
            }

            auto& hostWrite = soundHostWriteTrace_[
                soundHostWriteCount_ % soundHostWriteTrace_.size()];
            hostWrite.registerIndex = static_cast<std::uint8_t>(index);
            hostWrite.value = value;
            hostWrite.instructionPC = instructionPC;
            hostWrite.machineCycle = elapsedMachineCycles_;
            ++soundHostWriteCount_;

            // Only a contiguous 0,1,2,3 sequence is captured as a complete
            // four-register transaction. Other writes remain individual events.
            if (index == static_cast<std::size_t>(ExternalAudioRegister::register0)) {
                soundHostTransactionBytes_[0] = value;
                soundHostExpectedRegister_ = 1;
                soundHostTransactionActive_ = true;
            } else if (soundHostTransactionActive_ && index == soundHostExpectedRegister_) {
                soundHostTransactionBytes_[index] = value;
                ++soundHostExpectedRegister_;
                if (index == static_cast<std::size_t>(ExternalAudioRegister::register2)) {
                    soundHostBurstLowByte_ = value;
                    soundHostBurstLowInstructionPC_ = instructionPC;
                    soundHostBurstLowPending_ = true;
                } else if (index == static_cast<std::size_t>(ExternalAudioRegister::register3)) {
                    if (soundHostBurstLowPending_) {
                        auto& word = soundHostBurstWordTrace_[
                            soundHostBurstWordCount_ % soundHostBurstWordTrace_.size()];
                        word.lowByte = soundHostBurstLowByte_;
                        word.highByte = value;
                        word.value = static_cast<std::uint16_t>(
                            static_cast<std::uint16_t>(soundHostBurstLowByte_) |
                            (static_cast<std::uint16_t>(value) << 8u));
                        word.lowInstructionPC = soundHostBurstLowInstructionPC_;
                        word.highInstructionPC = instructionPC;
                        word.machineCycle = elapsedMachineCycles_;
                        ++soundHostBurstWordCount_;
                    }
                    soundHostBurstLowPending_ = false;

                    auto& packet = soundCommandTrace_[
                        soundCommandPacketCount_ % soundCommandTrace_.size()];
                    packet.bytes = soundHostTransactionBytes_;
                    packet.instructionPC = instructionPC;
                    const auto stackPointer = cpu_.registers().sp;
                    const auto returnHigh = readIndirect(stackPointer);
                    const auto returnLow = readIndirect(static_cast<std::uint8_t>(stackPointer - 1u));
                    packet.returnPC = static_cast<std::uint16_t>(
                        (static_cast<std::uint16_t>(returnHigh) << 8u) | returnLow);
                    packet.machineCycle = elapsedMachineCycles_;
                    ++soundCommandPacketCount_;
                    soundHostTransactionActive_ = false;
                    soundHostExpectedRegister_ = 0;
                }
            } else {
                soundHostTransactionActive_ = false;
                soundHostExpectedRegister_ = 0;
                soundHostBurstLowPending_ = false;
            }
            if (index != static_cast<std::size_t>(ExternalAudioRegister::register3))
                soundHostBurstLowPending_ = index == static_cast<std::size_t>(
                    ExternalAudioRegister::register2);
            return;
        }

        if (testExternalRam7000Enabled_ && address >= 0x7000u && address <= 0x7fffu) {
            testExternalRam7000_[address - 0x7000u] = value;
            if (address >= 0x7bd0u && address <= 0x7bd7u) {
                auto& event = externalWindowTrace_[externalWindowTraceCount_ % externalWindowTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = true;
                event.machineCycle = elapsedMachineCycles_;
                ++externalWindowTraceCount_;
            }
            return;
        }

        switch (address) {
            case 0xa000:
                aciaControl_ = value;
                if ((value & 0x03u) == 0x03u) {
                    aciaStatus_ = 0x02; // master reset; TDRE is masked on status reads
                    aciaReceive_ = 0;
                    aciaTransmitPending_ = false;
                    aciaTransmitterBusy_ = false;
                    aciaTxReadyCycle_ = 0;
                }
                refreshAciaInterrupt();
                break;
            case 0xa001:
                if ((aciaControl_ & 0x03u) != 0x03u && !aciaTransmitterBusy_) {
                    aciaTransmit_ = value;
                    aciaTransmitPending_ = true;
                    aciaStatus_ = static_cast<std::uint8_t>(aciaStatus_ & ~0x02u); // clear TDRE
                    aciaTransmitterBusy_ = true;
                    const auto bitCycles = std::max<std::uint64_t>(
                        1u, midiByteMachineCycles() / 10u);
                    aciaTxReadyCycle_ = elapsedMachineCycles_ + bitCycles;
                    refreshAciaInterrupt();
                }
                break;
            case 0xb000: lcdInstruction_ = value; break;
            case 0xb001: lcdData_ = value; break;
            default:
                if (address >= 0x3900u && address <= 0xbfffu) {
                    auto& event = unmappedBusTrace_[unmappedBusTraceCount_ % unmappedBusTrace_.size()];
                    event.address = address;
                    event.value = value;
                    event.write = true;
                    event.machineCycle = elapsedMachineCycles_;
                    ++unmappedBusTraceCount_;
                    if (address >= 0x7000u && address <= 0x7fffu)
                        ++unmappedWriteCounts7xxx_[address - 0x7000u];
                }
                break; // writes to unmapped/stub regions are ignored
        }
    }

    void advanceMachineCycles(unsigned cycles) noexcept override {
        for (unsigned cycle = 0; cycle < cycles; ++cycle) {
            ++elapsedMachineCycles_;
            deliverScheduledAciaBytes();
            if (aciaTransmitterBusy_ && elapsedMachineCycles_ >= aciaTxReadyCycle_) {
                aciaTransmitterBusy_ = false;
                aciaStatus_ = static_cast<std::uint8_t>(aciaStatus_ | 0x02u); // TDRE
                refreshAciaInterrupt();
            }
            timers_.advanceMachineCycles(*this, 1);
        }
    }

    void setPortPins(std::uint8_t portAddress, std::uint8_t value) noexcept {
        const int port = portIndex(portAddress);
        if (port >= 0) portPins_[static_cast<std::size_t>(port)] = value;
    }

    void injectAciaReceiveByte(std::uint8_t value) noexcept {
        // The firmware selects the ACIA's divide-by-64 mode (CR1:CR0=10),
        // which matches the 31.25 kbaud MIDI stream on the inferred 2 MHz clock.
        if ((aciaControl_ & 0x03u) != 0x02u)
            return;
        if ((aciaStatus_ & 0x01u) != 0)
            aciaStatus_ = static_cast<std::uint8_t>(aciaStatus_ | 0x20u); // overrun
        else
            aciaReceive_ = value;
        aciaStatus_ = static_cast<std::uint8_t>(aciaStatus_ | 0x01u); // RDRF
        refreshAciaInterrupt();
    }

    std::uint64_t midiByteMachineCycles() const noexcept {
        const auto numerator = static_cast<std::uint64_t>(machineCyclesPerSecond()) * 10u;
        return std::max<std::uint64_t>(1u, (numerator + 15625u) / 31250u);
    }

    bool scheduleAciaMidiBytes(const std::uint8_t* bytes, std::size_t size,
                               std::uint64_t firstArrivalCycle) noexcept {
        if (size == 0)
            return true;
        if (bytes == nullptr || size > scheduledAciaBytes_.size() - scheduledAciaCount_)
            return false;

        auto arrival = std::max({firstArrivalCycle, nextAciaReceiveCycle_, elapsedMachineCycles_});
        const auto spacing = midiByteMachineCycles();
        for (std::size_t i = 0; i < size; ++i) {
            arrival += spacing; // RDRF is asserted when the full 10-bit frame arrives
            scheduledAciaBytes_[scheduledAciaTail_] = {arrival, bytes[i]};
            scheduledAciaTail_ = (scheduledAciaTail_ + 1u) % scheduledAciaBytes_.size();
            ++scheduledAciaCount_;
        }
        nextAciaReceiveCycle_ = arrival;
        return true;
    }

    bool takeAciaTransmitByte(std::uint8_t& value) noexcept {
        if (!aciaTransmitPending_) return false;
        value = aciaTransmit_;
        aciaTransmitPending_ = false;
        return true;
    }

    const std::array<std::uint8_t, 4>& commandPortBytes() const noexcept {
        return commandPort_;
    }
    std::size_t soundCommandPacketCount() const noexcept { return soundCommandPacketCount_; }
    std::uint16_t soundHostWordForTesting(std::uint8_t index) const noexcept {
        return soundHostWords_[index];
    }
    std::size_t soundHostReadCount() const noexcept { return soundHostReadCount_; }
    const soundHostReadEvent& soundHostReadAt(std::size_t index) const noexcept {
        return soundHostReadTrace_[index % soundHostReadTrace_.size()];
    }
    std::size_t soundHostWriteCount() const noexcept { return soundHostWriteCount_; }
    const soundHostWriteEvent& soundHostWriteAt(std::size_t index) const noexcept {
        return soundHostWriteTrace_[index % soundHostWriteTrace_.size()];
    }
    std::size_t soundHostBurstWordCount() const noexcept { return soundHostBurstWordCount_; }
    const SoundHostBurstWordEvent& soundHostBurstWordAt(std::size_t index) const noexcept {
        return soundHostBurstWordTrace_[index % soundHostBurstWordTrace_.size()];
    }
    std::uint64_t aciaReceiveReadCount() const noexcept { return aciaReceiveReadCount_; }
    std::uint64_t aciaValidReceiveReadCount() const noexcept { return aciaValidReceiveReadCount_; }
    std::size_t scheduledAciaPendingCount() const noexcept { return scheduledAciaCount_; }
    std::uint8_t lastAciaReceiveRead() const noexcept { return lastAciaReceiveRead_; }
    std::uint8_t lastValidAciaReceiveRead() const noexcept { return lastValidAciaReceiveRead_; }
    const SoundCommandPacket& soundCommandPacketAt(std::size_t index) const noexcept {
        return soundCommandTrace_[index % soundCommandTrace_.size()];
    }
    std::size_t portWriteCount() const noexcept { return portWriteCount_; }
    const PortWriteEvent& portWriteAt(std::size_t index) const noexcept {
        return portWriteTrace_[index % portWriteTrace_.size()];
    }
    std::size_t portReadCount() const noexcept { return portReadCount_; }
    const PortReadEvent& portReadAt(std::size_t index) const noexcept {
        return portReadTrace_[index % portReadTrace_.size()];
    }

    std::uint8_t lastSoundDeviceWrite() const noexcept { return soundDeviceWrite_; }
    std::uint64_t soundMemoryReadCount() const noexcept { return soundMemoryReadCount_; }
    std::size_t soundMemoryReadTraceCount() const noexcept { return soundMemoryReadTraceCount_; }
    const SoundMemoryReadEvent& soundMemoryReadTraceAt(std::size_t index) const noexcept {
        return soundMemoryReadTrace_[index % soundMemoryReadTrace_.size()];
    }
    std::uint64_t soundMemoryWriteCount() const noexcept { return soundMemoryWriteCount_; }
    std::size_t soundMemoryWriteTraceCount() const noexcept { return soundMemoryWriteTraceCount_; }
    const SoundMemoryWriteEvent& soundMemoryWriteTraceAt(std::size_t index) const noexcept {
        return soundMemoryWriteTrace_[index % soundMemoryWriteTrace_.size()];
    }
    std::size_t ic51BusProbeTraceCount() const noexcept { return ic51BusProbeTraceCount_; }
    const IC51BusProbeEvent& ic51BusProbeTraceAt(std::size_t index) const noexcept {
        return ic51BusProbeTrace_[index % ic51BusProbeTrace_.size()];
    }
    std::uint64_t soundBusModeWriteCount() const noexcept { return soundBusModeWriteCount_; }
    std::uint64_t p35TransitionCount() const noexcept { return p35TransitionCount_; }
    std::uint16_t lastSoundMemoryWriteAddress() const noexcept { return lastSoundMemoryWriteAddress_; }
    std::uint8_t lastSoundMemoryWriteValue() const noexcept { return lastSoundMemoryWriteValue_; }
    std::uint8_t externalSoundBusMode() const noexcept { return internal_[0x8e]; }
    std::uint8_t xdataByteForTesting(std::uint16_t address) const noexcept {
        return xdata_[address];
    }
    std::uint64_t soundMemoryReadsByMode(std::size_t mode) const noexcept {
        return mode < soundMemoryReadsByMode_.size() ? soundMemoryReadsByMode_[mode] : 0;
    }
    std::uint64_t soundMemoryWritesByMode(std::size_t mode) const noexcept {
        return mode < soundMemoryWritesByMode_.size() ? soundMemoryWritesByMode_[mode] : 0;
    }
    void setSoundDeviceResponseForTesting(std::uint8_t value) noexcept {
        soundDeviceResponse_ = value;
    }
    // Experimental fixture only: MAME leaves the 0x7000-0x7FFF external
    // window unimplemented. This opt-in RAM overlay tests firmware control
    // flow without changing the default open-bus hardware model.
    void enableTestExternalRamWindow7000(std::uint8_t fillValue = 0x00) noexcept {
        testExternalRam7000_.fill(fillValue);
        testExternalRam7000Enabled_ = true;
    }
    void setTestExternalRamByte(std::uint16_t address, std::uint8_t value) noexcept {
        if (address >= 0x7000u && address <= 0x7fffu) {
            testExternalRam7000_[address - 0x7000u] = value;
            testExternalRam7000Enabled_ = true;
        }
    }
    std::uint8_t testExternalRamByteForTesting(std::uint16_t address) const noexcept {
        return address >= 0x7000u && address <= 0x7fffu
            ? testExternalRam7000_[address - 0x7000u] : 0xffu;
    }
    void setTestExternalReadOverride(std::uint16_t address, std::uint8_t value) noexcept {
        testExternalReadOverrideAddress_ = address;
        testExternalReadOverrideValue_ = value;
        testExternalReadOverrideEnabled_ = true;
    }
    std::size_t externalWindowTraceCount() const noexcept { return externalWindowTraceCount_; }
    const ExternalWindowEvent& externalWindowTraceAt(std::size_t index) const noexcept {
        return externalWindowTrace_[index % externalWindowTrace_.size()];
    }

    static constexpr std::uint8_t descrambledOffset(std::uint8_t offset) noexcept {
        constexpr std::uint8_t order[8] = {1, 2, 3, 6, 5, 7, 1, 0};
        std::uint8_t value = 0;
        for (const auto bit : order)
            value = static_cast<std::uint8_t>((value << 1u) | ((offset >> bit) & 1u));
        return static_cast<std::uint8_t>(value ^ 0x40u ^ ((offset & 0x11u) << 3u));
    }

private:
    static constexpr int portIndex(std::uint8_t address) noexcept {
        return address == 0x80u ? 0 : address == 0x90u ? 1 :
               address == 0xa0u ? 2 : address == 0xb0u ? 3 : -1;
    }

    bool isIC51Selected() const noexcept {
        const auto pins = portPins_[3];
        // The directory path (B3BC) asserts P3.4/P3.1 low; P3.5 selects
        // bank 0/1. The AF1A bulk loader uses the alternate P3 pattern
        // P3.4/P3.1 high with P3.5 low, which selects bank 0.
        const bool directorySelect = (pins & 0x12u) == 0;
        const bool bulkBlockSelect = (pins & 0x32u) == 0x12u;
        return ic51RomLoaded_ && (directorySelect || bulkBlockSelect);
    }

    static constexpr bool isDedicatedPeripheralAddress(std::uint16_t address) noexcept {
        return (address >= 0x8000u && address <= 0x8003u) ||
               (address >= 0xa002u && address <= 0xa003u) ||
               (address >= 0xb002u && address <= 0xb003u) ||
               address == 0xff10u;
    }

    static constexpr bool isXdataRam(std::uint16_t address) noexcept {
        // 0000-38FF is the mapped work RAM. IC4 routine B34D explicitly
        // writes and reads the complete 16-KiB C000-FFFF sound-memory window.
        return address <= 0x38ffu || address >= 0xc000u;
    }

    struct ScheduledAciaByte {
        std::uint64_t arrivalCycle = 0;
        std::uint8_t value = 0;
    };

    void deliverScheduledAciaBytes() noexcept {
        while (scheduledAciaCount_ != 0) {
            const auto& next = scheduledAciaBytes_[scheduledAciaHead_];
            if (next.arrivalCycle > elapsedMachineCycles_)
                break;
            injectAciaReceiveByte(next.value);
            scheduledAciaHead_ = (scheduledAciaHead_ + 1u) % scheduledAciaBytes_.size();
            --scheduledAciaCount_;
        }
    }

    void refreshAciaInterrupt() noexcept {
        // MC6850 status bit 7 and the 8032 INT1 pin share the same IRQ state.
        const bool active = (aciaControl_ & 0x03u) != 0x03u;
        const bool receiveIrq = (aciaControl_ & 0x80u) != 0 &&
            (aciaStatus_ & 0x01u) != 0;
        const bool transmitIrq = (aciaControl_ & 0x18u) == 0x08u &&
            (aciaStatus_ & 0x02u) != 0;
        const bool irqActive = active && (receiveIrq || transmitIrq);
        if (irqActive) {
            aciaStatus_ = static_cast<std::uint8_t>(aciaStatus_ | 0x80u);
            portPins_[3] = static_cast<std::uint8_t>(portPins_[3] & ~0x08u); // INT1 active low
        } else {
            aciaStatus_ = static_cast<std::uint8_t>(aciaStatus_ & ~0x80u);
            portPins_[3] = static_cast<std::uint8_t>(portPins_[3] | 0x08u);
        }
    }

    std::uint32_t oscillatorHz_ = kAssumedOscillatorHz;
    MCS51Core cpu_{};
    MCS51TimerUnit timers_{};
    std::array<std::uint8_t, kIC4RomSize> rom_{};
    std::array<std::uint8_t, kIC51RomSize> ic51Rom_{};
    std::array<std::uint8_t, kCodeSpaceSize> code_{};
    bool ic51RomLoaded_ = false;
    std::uint64_t ic51ReadCount_ = 0;
    std::array<std::uint64_t, 2> ic51ReadsByBank_{};
    std::uint16_t lastIC51ReadAddress_ = 0;
    std::array<IC51ReadEvent, 512> ic51ReadTrace_{};
    std::size_t ic51ReadTraceCount_ = 0;
    std::array<XdataWatchEvent, 512> xdataWatchTrace_{};
    std::size_t xdataWatchTraceCount_ = 0;
    std::array<UnmappedBusEvent, 512> unmappedBusTrace_{};
    std::size_t unmappedBusTraceCount_ = 0;
    std::array<std::uint64_t, 0x1000> unmappedReadCounts7xxx_{};
    std::array<std::uint64_t, 0x1000> unmappedWriteCounts7xxx_{};
    std::array<std::uint8_t, 0x100> internal_{};
    std::array<std::uint8_t, 0x80> upperIram_{};
    std::array<std::uint8_t, 0x10000> xdata_{};
    std::array<std::uint8_t, 4> portLatch_{{0xff, 0xff, 0xff, 0xff}};
    std::array<std::uint8_t, 4> portPins_{{0xff, 0xff, 0xff, 0xff}};
    std::array<std::uint8_t, 4> externalPortInputMask_{};
    std::array<std::uint8_t, 4> externalPortInputValue_{{0xff, 0xff, 0xff, 0xff}};
    std::array<std::uint8_t, 4> commandPort_{};
    std::uint8_t soundHostCommandLatch_ = 0;
    std::array<std::uint16_t, 256> soundHostWords_{};
    std::array<std::array<std::uint8_t, 3>, 256> soundHostData_{};
    std::array<SoundCommandPacket, 1024> soundCommandTrace_{};
    std::size_t soundCommandPacketCount_ = 0;
    std::array<soundHostReadEvent, 2048> soundHostReadTrace_{};
    std::size_t soundHostReadCount_ = 0;
    std::array<soundHostWriteEvent, 4096> soundHostWriteTrace_{};
    std::size_t soundHostWriteCount_ = 0;
    bool soundHostTransactionActive_ = false;
    std::uint8_t soundHostExpectedRegister_ = 0;
    std::array<std::uint8_t, 4> soundHostTransactionBytes_{};
    std::array<SoundHostBurstWordEvent, 2048> soundHostBurstWordTrace_{};
    std::size_t soundHostBurstWordCount_ = 0;
    bool soundHostBurstLowPending_ = false;
    std::uint8_t soundHostBurstLowByte_ = 0;
    std::uint16_t soundHostBurstLowInstructionPC_ = 0;
    std::array<SoundMemoryReadEvent, 8192> soundMemoryReadTrace_{};
    std::size_t soundMemoryReadTraceCount_ = 0;
    std::array<SoundMemoryWriteEvent, 8192> soundMemoryWriteTrace_{};
    std::size_t soundMemoryWriteTraceCount_ = 0;
    std::array<IC51BusProbeEvent, 256> ic51BusProbeTrace_{};
    std::size_t ic51BusProbeTraceCount_ = 0;
    std::array<PortWriteEvent, 256> portWriteTrace_{};
    std::size_t portWriteCount_ = 0;
    mutable std::array<PortReadEvent, 512> portReadTrace_{};
    mutable std::size_t portReadCount_ = 0;
    std::array<std::uint64_t, 8> watchedCallCounts_{};
    std::uint8_t aciaControl_ = 0;
    std::uint8_t aciaStatus_ = 0x02;
    std::uint8_t aciaReceive_ = 0;
    std::uint64_t aciaReceiveReadCount_ = 0;
    std::uint64_t aciaValidReceiveReadCount_ = 0;
    std::uint8_t lastAciaReceiveRead_ = 0;
    std::uint8_t lastValidAciaReceiveRead_ = 0;
    std::uint8_t aciaTransmit_ = 0;
    std::array<ScheduledAciaByte, 4096> scheduledAciaBytes_{};
    std::size_t scheduledAciaHead_ = 0;
    std::size_t scheduledAciaTail_ = 0;
    std::size_t scheduledAciaCount_ = 0;
    std::uint64_t nextAciaReceiveCycle_ = 0;
    bool aciaTransmitPending_ = false;
    bool aciaTransmitterBusy_ = false;
    std::uint64_t aciaTxReadyCycle_ = 0;
    std::uint8_t lcdStatus_ = 0;
    std::uint8_t lcdData_ = 0;
    std::uint8_t lcdInstruction_ = 0;
    std::uint64_t elapsedMachineCycles_ = 0;
    bool soundDeviceResetReleased_ = true;
    std::uint8_t soundDeviceWrite_ = 0;
    std::uint8_t soundDeviceResponse_ = 0xff;
    bool testExternalRam7000Enabled_ = false;
    std::array<std::uint8_t, 0x1000> testExternalRam7000_{};
    std::array<ExternalWindowEvent, 1024> externalWindowTrace_{};
    std::size_t externalWindowTraceCount_ = 0;
    bool testExternalReadOverrideEnabled_ = false;
    std::uint16_t testExternalReadOverrideAddress_ = 0;
    std::uint8_t testExternalReadOverrideValue_ = 0xff;
    std::uint64_t soundMemoryReadCount_ = 0;
    std::uint64_t soundMemoryWriteCount_ = 0;
    std::uint64_t soundBusModeWriteCount_ = 0;
    std::uint64_t p35TransitionCount_ = 0;
    std::uint16_t lastSoundMemoryWriteAddress_ = 0;
    std::uint8_t lastSoundMemoryWriteValue_ = 0;
    std::array<std::uint64_t, 4> soundMemoryReadsByMode_{};
    std::array<std::uint64_t, 4> soundMemoryWritesByMode_{};
    bool cpuFaulted_ = false;
    bool romLoaded_ = false;
};

} // namespace technox
