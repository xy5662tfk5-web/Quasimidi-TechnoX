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
    struct SoundHostBusContext {
        std::uint16_t instructionPC = 0;
        std::uint64_t machineCycle = 0;
        std::uint8_t deviceIndex = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool ic51Selected = false;
        bool soundDeviceResetReleased = true;
    };
    struct SoundCommandPacket {
        // Bus state sampled independently at each of the four writes.
        std::array<SoundHostBusContext, 4> byteContext{};
        // Snapshot of sequential writes to the undocumented 0x8000-0x8003
        // external register window. The register meanings are not established.
        std::array<std::uint8_t, 4> bytes{};
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint64_t machineCycle = 0;
        // Bus context at the final byte of the transaction. These are
        // observations, not decoded DSP register meanings.
        std::uint8_t deviceIndex = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool ic51Selected = false;
        bool soundDeviceResetReleased = true;
    };
    struct soundHostReadEvent {
        std::uint8_t registerIndex = 0;
        std::uint8_t value = 0xff;
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint64_t eventSequence = 0;
        std::uint64_t machineCycle = 0;
        std::uint8_t deviceIndex = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool ic51Selected = false;
        bool soundDeviceResetReleased = true;
        bool responseInjectedForTesting = false;
    };
    struct soundHostWriteEvent {
        std::uint8_t registerIndex = 0;
        std::uint8_t value = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint64_t eventSequence = 0;
        std::uint64_t machineCycle = 0;
        std::uint8_t deviceIndex = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool ic51Selected = false;
        bool soundDeviceResetReleased = true;
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
        std::uint8_t previousValue = 0xff;
        std::uint8_t value = 0xff;
        std::uint8_t changedMask = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        std::uint16_t instructionPC = 0;
        std::uint64_t eventSequence = 0;
        std::uint64_t machineCycle = 0;
    };
    struct P3Bit2ControlWriteEvent {
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint8_t previousPort3 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t port2 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool bit2Changed = false;
        std::uint64_t eventSequence = 0;
        std::uint64_t machineCycle = 0;
    };
    struct SoundBusModeWriteEvent {
        std::uint8_t previousValue = 0;
        std::uint8_t value = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        bool ic51Selected = false;
        std::uint64_t eventSequence = 0;
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
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool ic51Selected = false;
        std::uint64_t eventSequence = 0;
        std::uint64_t machineCycle = 0;
    };
    struct SoundMemoryWriteEvent {
        std::uint16_t address = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint8_t value = 0;
        std::uint8_t mode = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        bool ic51Selected = false;
        std::uint64_t eventSequence = 0;
        std::uint64_t machineCycle = 0;
    };
    struct SoundMemoryReadEvent {
        std::uint16_t address = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint8_t value = 0xff;
        std::uint8_t mode = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        bool ic51Selected = false;
        std::uint64_t eventSequence = 0;
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
    struct BulkTransferEntryEvent {
        std::uint16_t callPC = 0;
        std::uint16_t target = 0;
        std::uint8_t selectorR7 = 0;
        std::uint8_t p2 = 0xff;
        std::uint8_t p3 = 0xff;
        std::uint8_t soundBusMode = 0;
        std::uint8_t flagBit1B = 0;
        std::uint8_t globalInterruptEnabled = 0;
        std::uint64_t machineCycle = 0;
    };
    // Test-only snapshot taken immediately before the packet builder at 0x210F.
    // Register names are architectural 8051 names; no DSP semantics are implied.
    struct InstructionWatchEvent {
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint16_t dptr = 0;
        std::uint8_t accumulator = 0;
        std::uint8_t b = 0;
        std::uint8_t psw = 0;
        std::uint8_t registerBankBase = 0;
        std::array<std::uint8_t, 8> registers{};
        std::uint8_t directRam25 = 0;
        std::uint8_t directRam26 = 0;
        std::uint8_t directRam28 = 0;
        std::uint8_t directRam29 = 0;
        std::uint8_t directRam2F = 0;
        std::uint8_t directRam2A = 0;
        std::uint8_t directRam2B = 0;
        std::uint8_t directRam6E = 0;
        std::uint8_t directRam6F = 0;
        std::uint8_t directRam70 = 0;
        std::uint8_t directRam71 = 0;
        std::uint8_t directRam3C = 0;
        std::uint8_t directRam3D = 0;
        std::uint8_t directRam3E = 0;
        std::uint8_t directRam3F = 0;
        std::uint8_t directRam45 = 0;
        std::uint8_t directRam4E = 0;
        std::uint8_t directRam4F = 0;
        std::uint8_t directRam50 = 0;
        std::uint8_t directRam51 = 0;
        std::uint8_t directRam52 = 0;
        std::uint8_t directRam2C = 0;
        std::uint8_t directRam2D = 0;
        std::uint8_t directRam2E = 0;
        std::uint8_t directRam38 = 0;
        std::uint8_t directRam39 = 0;
        std::uint8_t directRam3A = 0;
        std::uint8_t directRam3B = 0;
        std::uint8_t directRam41 = 0;
        std::uint8_t directRam43 = 0;
        std::uint8_t directRam44 = 0;
        std::uint8_t directRam30 = 0;
        std::uint8_t directRam31 = 0;
        std::uint8_t directRam32 = 0;
        std::uint8_t directRam36 = 0;
        std::uint8_t directRam37 = 0;
        std::uint8_t directRam20 = 0;
        std::uint8_t port2Latch = 0xff;
        std::uint8_t port3Pins = 0xff;
        std::uint64_t machineCycle = 0;
    };
    // Dedicated bounded trace for IC4's indexed two-byte state calculator
    // (0x02D1). Kept separate from the broad instruction watch ring so the
    // additional probes cannot evict established MIDI-dispatch observations.
    struct IndexedPairMathWatchEvent {
        std::uint16_t instructionPC = 0;
        std::uint16_t returnPC = 0;
        std::uint16_t dptr = 0;
        std::uint8_t accumulator = 0;
        std::uint8_t b = 0;
        std::uint8_t psw = 0;
        std::array<std::uint8_t, 8> registers{};
        std::uint8_t ram28 = 0;
        std::uint8_t ram29 = 0;
        std::uint8_t ram2A = 0;
        std::uint8_t ram6B = 0;
        std::uint8_t ram6C = 0;
        std::uint8_t ram6E = 0;
        std::uint8_t ram6F = 0;
        std::uint8_t ram70 = 0;
        std::uint8_t ram71 = 0;
        std::uint64_t machineCycle = 0;
    };
    // Bounded execution trace from the two firmware helper-return sites to the
    // next Note On packet builder. Values are raw 8032 architectural state.
    struct ExecutionInstructionEvent {
        std::uint16_t windowStartPC = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint16_t dptr = 0;
        std::uint64_t machineCycle = 0;
        std::uint64_t dataAccessCount = 0;
        std::array<std::uint8_t, 3> opcodeBytes{};
        std::array<std::uint8_t, 8> registers{};
        // [0] is direct IRAM 0x2B; [1..8] are direct IRAM 0x30..0x37.
        std::array<std::uint8_t, 9> ram2BAnd30To37{};
        std::uint8_t accumulator = 0;
        std::uint8_t b = 0;
        std::uint8_t psw = 0;
        std::uint8_t registerBankBase = 0;
        std::uint8_t stackPointer = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
    };
    struct ExecutionDataAccessEvent {
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint16_t address = 0;
        std::uint64_t machineCycle = 0;
        std::uint8_t value = 0xff;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool write = false;
        bool ic51Selected = false;
    };
    struct InternalFlagWriteEvent {
        std::uint8_t bitAddress = 0x0a;
        std::uint8_t byteValue = 0;
        std::uint8_t opcode = 0;
        bool previousValue = false;
        bool currentValue = false;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint64_t machineCycle = 0;
    };
    // Writes to IRAM 0x36/0x37 are the two-byte result of the slot/sine
    // fixed-point stage. Keep both direct and @Ri writes with pre-write CPU state.
    struct InternalRamWriteEvent {
        std::uint8_t address = 0;
        std::uint8_t previousValue = 0;
        std::uint8_t value = 0;
        bool indirectAddressing = false;
        std::uint16_t instructionPC = 0;
        std::uint16_t pcAfterFetch = 0;
        std::uint16_t returnPC = 0;
        std::array<std::uint8_t, 3> opcodeBytes{};
        std::uint16_t dptr = 0;
        std::uint8_t accumulator = 0;
        std::uint8_t b = 0;
        std::uint8_t psw = 0;
        std::uint8_t registerBankBase = 0;
        std::array<std::uint8_t, 8> registers{};
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        std::uint64_t eventSequence = 0;
        std::uint64_t machineCycle = 0;
    };
    struct FF10ReadEvent {
        std::uint16_t address = 0xff10u;
        std::uint8_t value = 0xffu;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool ic51Selected = false;
        bool soundDeviceResetReleased = true;
        std::uint64_t eventSequence = 0;
        std::uint64_t machineCycle = 0;
    };
    struct FF10WriteEvent {
        std::uint16_t address = 0xff10u;
        std::uint8_t value = 0xffu;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool ic51Selected = false;
        bool soundDeviceResetReleased = true;
        std::uint64_t eventSequence = 0;
        std::uint64_t machineCycle = 0;
    };
    struct XdataWatchEvent {
        std::uint16_t address = 0;
        std::uint8_t value = 0xff;
        bool write = false;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool ic51Selected = false;
        bool writeAccepted = true;
        std::uint64_t machineCycle = 0;
    };
    // Narrow, non-wrapping-in-normal-note-use trace for the four-byte nested
    // resource tuple written by IC4 0x1A32/0x1A35/0x1A38/0x1A3C. The general
    // XDATA trace is deliberately broad and can roll over during long MIDI tests.
    struct ResourceTupleWriteEvent {
        std::uint16_t address = 0;
        std::uint8_t value = 0xff;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint64_t machineCycle = 0;
    };
    struct MidiEventGateAccessEvent {
        std::uint16_t address = 0;
        std::uint8_t value = 0xff;
        bool write = false;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint8_t soundBusMode = 0;
        bool ic51Selected = false;
        std::uint64_t machineCycle = 0;
    };
    struct DispatchFlagBackingWriteEvent {
        std::uint8_t address = 0;
        std::uint8_t previousValue = 0;
        std::uint8_t currentValue = 0;
        std::uint8_t opcode = 0;
        std::uint8_t operand = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint64_t machineCycle = 0;
    };
    struct SlotInterpolationDataAccessEvent {
        std::uint16_t address = 0;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint8_t value = 0xff;
        std::uint8_t port2 = 0xff;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool write = false;
        bool ic51Selected = false;
        std::uint64_t machineCycle = 0;
    };
    struct UnmappedBusEvent {
        std::uint16_t address = 0;
        std::uint8_t value = 0xff;
        bool write = false;
        std::uint16_t instructionPC = 0;
        std::uint16_t stackReturnPC = 0;
        std::uint8_t port3 = 0xff;
        std::uint8_t soundBusMode = 0;
        bool syntheticTestOverlay = false;
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
    // Test-only trace reset: leave CPU, ROM, bus state and cumulative counters untouched.
    void clearIC51ReadTraceForTesting() noexcept {
        ic51ReadTrace_.fill(IC51ReadEvent{});
        ic51ReadTraceCount_ = 0;
    }
    std::uint8_t ic51RomByteForTesting(std::size_t bank, std::uint16_t address) const noexcept {
        return bank < 2u ? ic51Rom_[bank * 0x10000u + address] : 0xffu;
    }
    std::size_t internalFlagWriteTraceCount() const noexcept {
        return internalFlagWriteTraceCount_;
    }
    const InternalFlagWriteEvent& internalFlagWriteTraceAt(std::size_t index) const noexcept {
        return internalFlagWriteTrace_[index % internalFlagWriteTrace_.size()];
    }
    std::size_t xdataWatchTraceCount() const noexcept { return xdataWatchTraceCount_; }
    const XdataWatchEvent& xdataWatchTraceAt(std::size_t index) const noexcept {
        return xdataWatchTrace_[index % xdataWatchTrace_.size()];
    }
    void clearResourceTupleWriteTraceForTesting() noexcept {
        resourceTupleWriteTrace_.fill(ResourceTupleWriteEvent{});
        resourceTupleWriteTraceCount_ = 0;
    }
    std::size_t resourceTupleWriteTraceCountForTesting() const noexcept {
        return resourceTupleWriteTraceCount_;
    }
    const ResourceTupleWriteEvent& resourceTupleWriteTraceAtForTesting(
        std::size_t index) const noexcept {
        return resourceTupleWriteTrace_[index % resourceTupleWriteTrace_.size()];
    }
    void clearMidiEventGateTraceForTesting() noexcept {
        midiEventGateTrace_.fill(MidiEventGateAccessEvent{});
        midiEventGateTraceCount_ = 0;
    }
    std::size_t midiEventGateTraceCountForTesting() const noexcept {
        return midiEventGateTraceCount_;
    }
    const MidiEventGateAccessEvent& midiEventGateTraceAtForTesting(
        std::size_t index) const noexcept {
        return midiEventGateTrace_[index % midiEventGateTrace_.size()];
    }
    void clearDispatchFlagWriteTraceForTesting() noexcept {
        dispatchFlagWriteTrace_.fill(DispatchFlagBackingWriteEvent{});
        dispatchFlagWriteTraceCount_ = 0;
    }
    std::size_t dispatchFlagWriteTraceCountForTesting() const noexcept {
        return dispatchFlagWriteTraceCount_;
    }
    const DispatchFlagBackingWriteEvent& dispatchFlagWriteTraceAtForTesting(
        std::size_t index) const noexcept {
        return dispatchFlagWriteTrace_[index % dispatchFlagWriteTrace_.size()];
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
        externalPort1P3Mask_ = 0;
        externalPort1P3LowValue_ = 0xff;
        externalPort1P3HighValue_ = 0xff;
        externalPort1P3SelectEnabled_ = false;
        commandPort_.fill(0);
        soundHostCommandLatch_ = 0;
        soundHostWords_.fill(0);
        for (auto& bytes : soundHostData_) bytes.fill(0);
        for (auto& bytes : soundHostResponseData_) bytes.fill(0xff);
        soundHostResponseInjectedForTesting_.fill(false);
        soundCommandPacketCount_ = 0;
        soundCommandTrace_.fill(SoundCommandPacket{});
        soundHostReadCount_ = 0;
        soundHostReadTrace_.fill(soundHostReadEvent{});
        soundHostWriteCount_ = 0;
        soundHostWriteTrace_.fill(soundHostWriteEvent{});
        soundHostTransactionActive_ = false;
        soundHostExpectedRegister_ = 0;
        soundHostTransactionBytes_.fill(0);
        soundHostTransactionContext_.fill(SoundHostBusContext{});
        ff10ReadTrace_.fill(FF10ReadEvent{});
        ff10ReadCount_ = 0;
        ff10WriteTrace_.fill(FF10WriteEvent{});
        ff10WriteCount_ = 0;
        soundHostBurstWordCount_ = 0;
        soundHostBurstWordTrace_.fill(SoundHostBurstWordEvent{});
        soundHostBurstLowPending_ = false;
        soundHostBurstLowByte_ = 0;
        soundHostBurstLowInstructionPC_ = 0;
        portWriteTrace_.fill(PortWriteEvent{});
        portWriteCount_ = 0;
        soundBusModeWriteTrace_.fill(SoundBusModeWriteEvent{});
        soundBusModeWriteTraceCount_ = 0;
        p3Bit2ControlWriteTrace_.fill(P3Bit2ControlWriteEvent{});
        p3Bit2ControlWriteCount_ = 0;
        portReadTrace_.fill(PortReadEvent{});
        portReadCount_ = 0;
        watchedCallCounts_.fill(0);
        instructionWatchTrace_.fill(InstructionWatchEvent{});
        instructionWatchCount_ = 0;
        slotInterpolationDataAccessTrace_.fill(SlotInterpolationDataAccessEvent{});
        slotInterpolationDataAccessTraceCount_ = 0;
        pitchSourceDataAccessTrace_.fill(SlotInterpolationDataAccessEvent{});
        pitchSourceDataAccessTraceCount_ = 0;
        executionInstructionTrace_.fill(ExecutionInstructionEvent{});
        executionInstructionTraceCount_ = 0;
        executionDataAccessTrace_.fill(ExecutionDataAccessEvent{});
        executionDataAccessTraceCount_ = 0;
        executionTraceWindowStartPC_ = 0;
        executionTraceArmed_ = false;
        executionTraceActive_ = false;
        ic51ReadTrace_.fill(IC51ReadEvent{});
        ic51ReadTraceCount_ = 0;
        xdataWatchTrace_.fill(XdataWatchEvent{});
        xdataWatchTraceCount_ = 0;
        resourceTupleWriteTrace_.fill(ResourceTupleWriteEvent{});
        resourceTupleWriteTraceCount_ = 0;
        midiEventGateTrace_.fill(MidiEventGateAccessEvent{});
        midiEventGateTraceCount_ = 0;
        dispatchFlagWriteTrace_.fill(DispatchFlagBackingWriteEvent{});
        dispatchFlagWriteTraceCount_ = 0;
        internalFlagWriteTrace_.fill(InternalFlagWriteEvent{});
        internalFlagWriteTraceCount_ = 0;
        internalRamWriteTrace_.fill(InternalRamWriteEvent{});
        internalRamWriteTraceCount_ = 0;
        bulkTransferEntryTrace_.fill(BulkTransferEntryEvent{});
        bulkTransferEntryCount_ = 0;
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
        traceEventSequence_ = 0;
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

    std::size_t bulkTransferEntryCount() const noexcept {
        return bulkTransferEntryCount_;
    }
    const BulkTransferEntryEvent& bulkTransferEntryAt(std::size_t index) const noexcept {
        return bulkTransferEntryTrace_[index % bulkTransferEntryTrace_.size()];
    }

    // Narrow test watch points: 0x1848 captures fixed-point helper inputs;
    // 0x210F captures registers immediately before the velocity-sensitive
    // packet builder that calls the four-byte writer at 0x211F.
    void clearInstructionWatchForTesting() noexcept {
        instructionWatchTrace_.fill(InstructionWatchEvent{});
        instructionWatchCount_ = 0;
    }
    std::size_t instructionWatchCountForTesting() const noexcept {
        return instructionWatchCount_;
    }
    const InstructionWatchEvent& instructionWatchAtForTesting(
        std::size_t index) const noexcept {
        return instructionWatchTrace_[index % instructionWatchTrace_.size()];
    }
    void clearIndexedPairMathWatchForTesting() noexcept {
        indexedPairMathWatchTrace_.fill(IndexedPairMathWatchEvent{});
        indexedPairMathWatchCount_ = 0;
    }
    std::size_t indexedPairMathWatchCountForTesting() const noexcept {
        return indexedPairMathWatchCount_;
    }
    std::size_t indexedPairMathWatchCapacityForTesting() const noexcept {
        return indexedPairMathWatchTrace_.size();
    }
    const IndexedPairMathWatchEvent& indexedPairMathWatchAtForTesting(
        std::size_t index) const noexcept {
        return indexedPairMathWatchTrace_[index % indexedPairMathWatchTrace_.size()];
    }
    void clearSlotInterpolationDataAccessTraceForTesting() noexcept {
        slotInterpolationDataAccessTrace_.fill(SlotInterpolationDataAccessEvent{});
        slotInterpolationDataAccessTraceCount_ = 0;
    }
    std::size_t slotInterpolationDataAccessTraceCountForTesting() const noexcept {
        return slotInterpolationDataAccessTraceCount_;
    }
    const SlotInterpolationDataAccessEvent& slotInterpolationDataAccessTraceAtForTesting(
        std::size_t index) const noexcept {
        return slotInterpolationDataAccessTrace_[
            index % slotInterpolationDataAccessTrace_.size()];
    }
    void clearPitchSourceDataAccessTraceForTesting() noexcept {
        pitchSourceDataAccessTrace_.fill(SlotInterpolationDataAccessEvent{});
        pitchSourceDataAccessTraceCount_ = 0;
    }
    std::size_t pitchSourceDataAccessTraceCountForTesting() const noexcept {
        return pitchSourceDataAccessTraceCount_;
    }
    const SlotInterpolationDataAccessEvent& pitchSourceDataAccessTraceAtForTesting(
        std::size_t index) const noexcept {
        return pitchSourceDataAccessTrace_[
            index % pitchSourceDataAccessTrace_.size()];
    }

    // Arms capture only when firmware reaches 0x1FC9 or 0x2027, and each
    // window ends after the pre-instruction snapshot at 0x210F.
    void clearExecutionTraceForTesting() noexcept {
        executionInstructionTrace_.fill(ExecutionInstructionEvent{});
        executionInstructionTraceCount_ = 0;
        executionDataAccessTrace_.fill(ExecutionDataAccessEvent{});
        executionDataAccessTraceCount_ = 0;
        executionTraceWindowStartPC_ = 0;
        executionTraceArmed_ = true;
        executionTraceActive_ = false;
    }
    void disableExecutionTraceForTesting() noexcept {
        executionTraceArmed_ = false;
        executionTraceActive_ = false;
    }
    std::size_t executionInstructionTraceCountForTesting() const noexcept {
        return executionInstructionTraceCount_;
    }
    std::size_t executionInstructionTraceRetainedCountForTesting() const noexcept {
        return std::min(executionInstructionTraceCount_, executionInstructionTrace_.size());
    }
    const ExecutionInstructionEvent& executionInstructionTraceAtForTesting(
        std::size_t absoluteIndex) const noexcept {
        return executionInstructionTrace_[absoluteIndex % executionInstructionTrace_.size()];
    }
    std::size_t executionDataAccessTraceCountForTesting() const noexcept {
        return executionDataAccessTraceCount_;
    }
    std::size_t executionDataAccessTraceRetainedCountForTesting() const noexcept {
        return std::min(executionDataAccessTraceCount_, executionDataAccessTrace_.size());
    }
    const ExecutionDataAccessEvent& executionDataAccessTraceAtForTesting(
        std::size_t absoluteIndex) const noexcept {
        return executionDataAccessTrace_[absoluteIndex % executionDataAccessTrace_.size()];
    }

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
            case 0x16c3: return watchedCallCounts_[8];
            case 0x189d: return watchedCallCounts_[9];
            case 0x1009: return watchedCallCounts_[10];
            case 0x018d: return watchedCallCounts_[11];
            case 0x1780: return watchedCallCounts_[12];
            case 0x17d4: return watchedCallCounts_[13];
            case 0x0e8e: return watchedCallCounts_[14];
            case 0x1456: return watchedCallCounts_[15];
            case 0x1742: return watchedCallCounts_[16];
            case 0x4803: return watchedCallCounts_[17];
            case 0x02d1: return watchedCallCounts_[18];
            case 0x1848: return watchedCallCounts_[19];
            case 0x0e3a: return watchedCallCounts_[20];
            case 0x26a2: return watchedCallCounts_[21];
            case 0x0e21: return watchedCallCounts_[22];
            case 0x0df7: return watchedCallCounts_[23];
            default: return 0;
        }
    }

    MCS51StepResult step() noexcept {
        const auto pc = cpu_.registers().pc;
        if (pc == 0x02d1u || pc == 0x02dfu || pc == 0x02e0u ||
            pc == 0x02e2u ||
            pc == 0x0339u || pc == 0x034fu || pc == 0x0354u ||
            pc == 0x0358u ||
            pc == 0x035cu || pc == 0x0392u || pc == 0x039eu ||
            pc == 0x03a2u)
            recordIndexedPairMathWatch(pc);
        if (executionTraceArmed_ && !executionTraceActive_ &&
            (pc == 0x1fc9u || pc == 0x2027u)) {
            executionTraceActive_ = true;
            executionTraceWindowStartPC_ = pc;
        }
        if (executionTraceActive_) {
            const auto& registers = cpu_.registers();
            auto& event = executionInstructionTrace_[
                executionInstructionTraceCount_ % executionInstructionTrace_.size()];
            event.windowStartPC = executionTraceWindowStartPC_;
            event.instructionPC = pc;
            event.stackReturnPC = currentStackReturnPC();
            event.dptr = registers.dptr;
            event.machineCycle = elapsedMachineCycles_;
            event.dataAccessCount = executionDataAccessTraceCount_;
            event.opcodeBytes = {{
                readCode(pc),
                readCode(static_cast<std::uint16_t>(pc + 1u)),
                readCode(static_cast<std::uint16_t>(pc + 2u))
            }};
            event.accumulator = registers.a;
            event.b = registers.b;
            event.psw = registers.psw.pack();
            event.registerBankBase = registers.psw.registerBankBase();
            event.stackPointer = registers.sp;
            event.port2 = portPins_[2];
            event.port3 = portPins_[3];
            event.soundBusMode = internal_[0x8e];
            for (std::size_t index = 0; index < event.registers.size(); ++index)
                event.registers[index] = readInternal(static_cast<std::uint8_t>(
                    event.registerBankBase + index));
            event.ram2BAnd30To37[0] = readInternal(0x2bu);
            for (std::size_t index = 0; index < 8; ++index)
                event.ram2BAnd30To37[index + 1u] = readInternal(
                    static_cast<std::uint8_t>(0x30u + index));
            ++executionInstructionTraceCount_;
            if (pc == 0x210fu)
                executionTraceActive_ = false;
        }
        if (pc == 0x210fu || pc == 0x1848u ||
            pc == 0x1fc9u || pc == 0x2027u ||
            pc == 0x5671u || pc == 0x4256u ||
            pc == 0x1494u || pc == 0xac00u || pc == 0xac16u ||
            pc == 0xac25u || pc == 0xac88u || pc == 0xaca0u ||
            pc == 0xacb2u || pc == 0x189du ||
            pc == 0x593du || pc == 0x5940u || pc == 0x5941u ||
            pc == 0x5942u || pc == 0x5945u || pc == 0x5948u ||
            pc == 0x594cu || pc == 0x5958u || pc == 0x5962u ||
            pc == 0x596cu || pc == 0x5976u || pc == 0x597eu ||
            pc == 0x598au || pc == 0x598du || pc == 0x5999u ||
            pc == 0x599cu || pc == 0x59a8u || pc == 0x59abu ||
            pc == 0x59b7u ||
            pc == 0x18ebu || pc == 0x18eeu ||
            pc == 0x0000u || pc == 0xeb32u ||
            pc == 0x5697u || pc == 0x569au || pc == 0x569du ||
            pc == 0x56a0u || pc == 0x56a1u || pc == 0x56a3u ||
            pc == 0x56a5u || pc == 0x56a7u || pc == 0x56a9u ||
            pc == 0x56acu || pc == 0x56afu || pc == 0x56b0u ||
            pc == 0x56b2u || pc == 0x56b5u || pc == 0x56beu ||
            pc == 0x56bfu || pc == 0x56c5u || pc == 0x56c8u ||
            pc == 0x4323u || pc == 0x4340u || pc == 0x434bu ||
            pc == 0x4375u || pc == 0x438bu || pc == 0x43a1u ||
            pc == 0x43b1u || pc == 0x43c7u || pc == 0x43d0u ||
            pc == 0x43d3u || pc == 0x43d5u || pc == 0x43e0u ||
            pc == 0x43e8u || pc == 0x43ebu || pc == 0x43f2u ||
            pc == 0x4403u || pc == 0x4406u || pc == 0x4407u ||
            pc == 0x4417u || pc == 0x442cu || pc == 0x442fu ||
            pc == 0x4430u || pc == 0x4440u || pc == 0x4455u ||
            pc == 0x4458u || pc == 0x4459u || pc == 0x4467u ||
            pc == 0x446au ||
            pc == 0x16a4u || pc == 0x16a7u || pc == 0x16afu ||
            pc == 0x16b0u || pc == 0x16c3u || pc == 0x16cau ||
            pc == 0x16d8u || pc == 0x16dbu || pc == 0x16ddu ||
            pc == 0x16e4u || pc == 0x16e6u || pc == 0x16f7u ||
            pc == 0x1700u || pc == 0x171du || pc == 0x1720u ||
            pc == 0x172du || pc == 0x1734u || pc == 0x1742u ||
            pc == 0x1749u || pc == 0x174cu || pc == 0x174eu ||
            pc == 0x174fu || pc == 0x177eu || pc == 0x1780u ||
            pc == 0x1788u || pc == 0x1789u || pc == 0x1792u ||
            pc == 0x179au || pc == 0x179eu || pc == 0x179fu ||
            pc == 0x17acu || pc == 0x17b4u || pc == 0x17bbu ||
            pc == 0x17c3u || pc == 0x17c7u || pc == 0x17c8u ||
            pc == 0x17d4u || pc == 0x17dau || pc == 0x17e6u ||
            pc == 0x17eau || pc == 0x17ebu || pc == 0x17f8u ||
            pc == 0x1800u || pc == 0x1807u || pc == 0x1813u ||
            pc == 0x1817u || pc == 0x1818u || pc == 0x182bu ||
            pc == 0x1834u || pc == 0x1837u || pc == 0x183bu ||
            pc == 0x183cu || pc == 0x1841u || pc == 0x1848u ||
            pc == 0x57a7u || pc == 0x9968u || pc == 0x9987u ||
            pc == 0x9988u || pc == 0x9994u || pc == 0x9997u ||
            pc == 0x99b5u || pc == 0x99b8u || pc == 0x99c5u ||
            pc == 0x9a14u ||
            pc == 0x189du || pc == 0x18a6u || pc == 0x18b8u ||
            pc == 0x18c0u || pc == 0x18d7u || pc == 0x18dbu ||
            pc == 0x18dfu || pc == 0x18e1u || pc == 0x18ebu ||
            pc == 0x18f1u || pc == 0x18f7u || pc == 0x1901u ||
            pc == 0x1906u || pc == 0x1911u || pc == 0x191cu ||
            pc == 0x191eu || pc == 0x1920u || pc == 0x1923u ||
            pc == 0x1927u || pc == 0x1929u || pc == 0x192au ||
            pc == 0x192du || pc == 0x1938u || pc == 0x1942u ||
            pc == 0x1958u || pc == 0x195cu || pc == 0x195fu ||
            pc == 0x196bu || pc == 0x1975u || pc == 0x1978u ||
            pc == 0x197eu || pc == 0x1986u || pc == 0x1995u ||
            pc == 0x199cu || pc == 0x199fu || pc == 0x19a2u ||
            pc == 0x19a4u || pc == 0x19a7u || pc == 0x19b1u ||
            pc == 0x19b7u || pc == 0x19b9u || pc == 0x19bcu ||
            pc == 0x19c1u || pc == 0x19cdu || pc == 0x19d0u ||
            pc == 0x19d3u || pc == 0x19dbu || pc == 0x19e0u ||
            pc == 0x19e1u || pc == 0x19e4u || pc == 0x19e9u ||
            pc == 0x19efu || pc == 0x19f1u || pc == 0x19f8u ||
            pc == 0x19fbu || pc == 0x19feu || pc == 0x1a01u ||
            pc == 0x1a07u || pc == 0x1a11u || pc == 0x1a16u ||
            pc == 0x1a17u || pc == 0x1a1au || pc == 0x1a1du ||
            pc == 0x1a1fu || pc == 0x1a31u || pc == 0x1a3eu ||
            pc == 0x1a44u || pc == 0x1a4au || pc == 0x1a4bu ||
            pc == 0x1a4cu || pc == 0x1a4eu || pc == 0x1a50u || pc == 0x1a54u ||
            pc == 0x1a57u || pc == 0x1a5au || pc == 0x1a74u ||
            pc == 0x1a76u || pc == 0x1a7eu || pc == 0x1a82u ||
            pc == 0x1a88u || pc == 0x1a9fu || pc == 0x1aa5u ||
            pc == 0x1a31u || pc == 0x1a3eu || pc == 0x1a4au ||
            pc == 0x1a4eu || pc == 0x1a50u || pc == 0x1a8cu ||
            pc == 0x1a8fu || pc == 0x1a99u || pc == 0x1a9fu ||
            pc == 0x1aa6u || pc == 0x1aa9u || pc == 0x1aafu ||
            pc == 0x1ab3u || pc == 0x1acbu || pc == 0x1ad4u ||
            pc == 0x1ae2u || pc == 0x1ae4u || pc == 0x1aeeu ||
            pc == 0x1af1u || pc == 0x1af4u ||
            pc == 0x1b00u || pc == 0x1b20u || pc == 0x1b40u ||
            pc == 0x1b60u || pc == 0x1b80u || pc == 0x1ba0u ||
            pc == 0x1bb1u || pc == 0x1bc3u || pc == 0x1c2bu ||
            pc == 0x1c61u || pc == 0x1c76u || pc == 0x1c77u ||
            pc == 0x1c7au || pc == 0x1c7bu || pc == 0x1c7du ||
            pc == 0x1c7eu || pc == 0x1c80u || pc == 0x1c81u ||
            pc == 0x1c83u || pc == 0x1c84u || pc == 0x1c86u ||
            pc == 0x1c87u || pc == 0x1c89u || pc == 0x1c8au ||
            pc == 0x1c91u ||
            pc == 0x1c97u || pc == 0x1c9bu || pc == 0x1c9cu || pc == 0x1c9eu ||
            pc == 0x1ca0u || pc == 0x1ca3u || pc == 0x1ca6u ||
            pc == 0x1ca8u || pc == 0x1caau || pc == 0x1cabu ||
            pc == 0x1caeu || pc == 0x1cb0u || pc == 0x1cb1u || pc == 0x1cb2u ||
            pc == 0x1cb3u || pc == 0x1cb4u || pc == 0x1cb5u || pc == 0x1cb6u ||
            pc == 0x1cb7u || pc == 0x1cbeu || pc == 0x1cbfu || pc == 0x1cc1u ||
            pc == 0x1cc4u || pc == 0x1ccbu ||
            pc == 0x1cabu ||
            pc == 0x1e5bu || pc == 0x1e5cu ||
            pc == 0x1e5eu || pc == 0x1e60u ||
            pc == 0x1e61u || pc == 0x1e62u || pc == 0x1e63u ||
            pc == 0x1e64u || pc == 0x1e65u || pc == 0x1e66u ||
            (pc >= 0x1e00u && pc <= 0x1e5fu) ||
            pc == 0x1eddu || pc == 0x224cu ||
            pc == 0x2301u || pc == 0x2374u ||
            pc == 0x09b9u || pc == 0x09dau ||
            pc == 0x0a4fu || pc == 0x22f8u ||
            pc == 0x211fu || pc == 0x236bu) {
            const auto& registers = cpu_.registers();
            auto& event = instructionWatchTrace_[
                instructionWatchCount_ % instructionWatchTrace_.size()];
            event.instructionPC = pc;
            event.dptr = registers.dptr;
            event.accumulator = registers.a;
            event.b = registers.b;
            event.psw = registers.psw.pack();
            event.registerBankBase = registers.psw.registerBankBase();
            for (std::size_t index = 0; index < event.registers.size(); ++index)
                event.registers[index] = readInternal(static_cast<std::uint8_t>(
                    event.registerBankBase + index));
            event.directRam25 = readInternal(0x25u);
            event.directRam26 = readInternal(0x26u);
            event.directRam28 = readInternal(0x28u);
            event.directRam29 = readInternal(0x29u);
            event.directRam2F = readInternal(0x2fu);
            event.directRam2A = readInternal(0x2au);
            event.directRam2B = readInternal(0x2bu);
            event.directRam6E = readInternal(0x6eu);
            event.directRam6F = readInternal(0x6fu);
            event.directRam70 = readInternal(0x70u);
            event.directRam71 = readInternal(0x71u);
            event.directRam3C = readInternal(0x3cu);
            event.directRam3D = readInternal(0x3du);
            event.directRam3E = readInternal(0x3eu);
            event.directRam3F = readInternal(0x3fu);
            event.directRam45 = readInternal(0x45u);
            event.directRam4E = readInternal(0x4eu);
            event.directRam4F = readInternal(0x4fu);
            event.directRam50 = readInternal(0x50u);
            event.directRam51 = readInternal(0x51u);
            event.directRam52 = readInternal(0x52u);
            event.directRam2C = readInternal(0x2cu);
            event.directRam2D = readInternal(0x2du);
            event.directRam2E = readInternal(0x2eu);
            event.directRam38 = readInternal(0x38u);
            event.directRam39 = readInternal(0x39u);
            event.directRam3A = readInternal(0x3au);
            event.directRam3B = readInternal(0x3bu);
            event.directRam41 = readInternal(0x41u);
            event.directRam43 = readInternal(0x43u);
            event.directRam44 = readInternal(0x44u);
            event.directRam30 = readInternal(0x30u);
            event.directRam31 = readInternal(0x31u);
            event.directRam32 = readInternal(0x32u);
            event.directRam36 = readInternal(0x36u);
            event.directRam37 = readInternal(0x37u);
            event.directRam20 = readInternal(0x20u);
            event.port2Latch = portLatch_[2];
            event.port3Pins = portPins_[3];
            const auto stackPointer = registers.sp;
            const auto returnHigh = readIndirect(stackPointer);
            const auto returnLow = readIndirect(static_cast<std::uint8_t>(stackPointer - 1u));
            event.returnPC = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(returnHigh) << 8u) | returnLow);
            event.machineCycle = elapsedMachineCycles_;
            ++instructionWatchCount_;
        }
        if (code_[pc] == 0x12u) {
            const auto target = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(code_[static_cast<std::uint16_t>(pc + 1u)]) << 8) |
                code_[static_cast<std::uint16_t>(pc + 2u)]);
            switch (target) {
                case 0xaf1a: {
                    auto& event = bulkTransferEntryTrace_[
                        bulkTransferEntryCount_ % bulkTransferEntryTrace_.size()];
                    const auto registerBank = cpu_.registers().psw.registerBankBase();
                    event.callPC = pc;
                    event.target = target;
                    event.selectorR7 = readInternal(static_cast<std::uint8_t>(registerBank + 7u));
                    event.p2 = portLatch_[2];
                    event.p3 = portLatch_[3];
                    event.soundBusMode = internal_[0x8e];
                    event.flagBit1B = static_cast<std::uint8_t>((internal_[0x23u] >> 3u) & 0x01u);
                    event.globalInterruptEnabled = static_cast<std::uint8_t>((readInternal(0xa8u) >> 7u) & 0x01u);
                    event.machineCycle = elapsedMachineCycles_;
                    ++bulkTransferEntryCount_;
                    break;
                }
                case 0x57a8: ++watchedCallCounts_[0]; break;
                case 0x5671: ++watchedCallCounts_[1]; break;
                case 0x4256: ++watchedCallCounts_[2]; break;
                case 0x1494: ++watchedCallCounts_[3]; break;
                case 0x0db7: ++watchedCallCounts_[4]; break;
                case 0xac00: ++watchedCallCounts_[5]; break;
                case 0xac88: ++watchedCallCounts_[6]; break;
                case 0xb34d: ++watchedCallCounts_[7]; break;
                case 0x16c3: ++watchedCallCounts_[8]; break;
                case 0x189d: ++watchedCallCounts_[9]; break;
                case 0x1009: ++watchedCallCounts_[10]; break;
                case 0x018d: ++watchedCallCounts_[11]; break;
                case 0x1780: ++watchedCallCounts_[12]; break;
                case 0x17d4: ++watchedCallCounts_[13]; break;
                case 0x0e8e: ++watchedCallCounts_[14]; break;
                case 0x1456: ++watchedCallCounts_[15]; break;
                case 0x1742: ++watchedCallCounts_[16]; break;
                case 0x4803: ++watchedCallCounts_[17]; break;
                case 0x02d1: ++watchedCallCounts_[18]; break;
                case 0x1848: ++watchedCallCounts_[19]; break;
                case 0x0e3a: ++watchedCallCounts_[20]; break;
                case 0x26a2: ++watchedCallCounts_[21]; break;
                case 0x0e21: ++watchedCallCounts_[22]; break;
                case 0x0df7: ++watchedCallCounts_[23]; break;
                default: break;
            }
            if (testInternalWriteOnCallEnabled_ &&
                target == testInternalWriteOnCallTarget_) {
                internal_[testInternalWriteOnCallAddress_] = testInternalWriteOnCallValue_;
                testInternalWriteOnCallEnabled_ = false;
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

    // Test/integration seam for an independently modelled external audio device.
    // Until its protocol is identified, the default response remains open bus.
    void setSoundHostResponseForTesting(std::uint8_t index,
                                        const std::array<std::uint8_t, 3>& response) noexcept {
        soundHostResponseData_[index] = response;
        soundHostResponseInjectedForTesting_[index] = true;
    }

    const std::array<std::uint8_t, 3>& soundHostPayloadForTesting(
        std::uint8_t index) const noexcept { return soundHostData_[index]; }

    std::uint8_t readCode(std::uint16_t address) const noexcept override {
        return code_[address];
    }

    std::uint8_t readInternal(std::uint8_t address) const noexcept override {
        const int port = portIndex(address);
        if (port < 0) return internal_[address];
        const auto index = static_cast<std::size_t>(port);
        auto mask = externalPortInputMask_[index];
        auto inputValue = externalPortInputValue_[index];

        // Optional test seam for multiplexed panel hardware: some external
        // input bits can depend on the P3.0 select state driven by IC4.
        // This is deliberately a fixture hook, not an assumed board decode.
        if (port == 1 && externalPort1P3SelectEnabled_) {
            const bool p3SelectHigh = (portPins_[3] & 0x01u) != 0;
            const auto dynamicValue = p3SelectHigh
                ? externalPort1P3HighValue_ : externalPort1P3LowValue_;
            mask = static_cast<std::uint8_t>(mask | externalPort1P3Mask_);
            inputValue = static_cast<std::uint8_t>(
                (inputValue & static_cast<std::uint8_t>(~externalPort1P3Mask_)) |
                (dynamicValue & externalPort1P3Mask_));
        }
        const auto value = static_cast<std::uint8_t>(
            (portPins_[index] & static_cast<std::uint8_t>(~mask)) |
            (inputValue & mask));
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

    // Test fixture for multiplexed external inputs whose returned levels
    // depend on the P3.0 selector driven by firmware scan code. The two values
    // model different external rows without assigning physical chip semantics.
    void setExternalPort1InputsByP3Bit0ForTesting(
        std::uint8_t mask, std::uint8_t whenP3Low,
        std::uint8_t whenP3High) noexcept {
        externalPort1P3Mask_ = mask;
        externalPort1P3LowValue_ = whenP3Low;
        externalPort1P3HighValue_ = whenP3High;
        externalPort1P3SelectEnabled_ = true;
    }

    void clearExternalPort1InputsByP3Bit0ForTesting() noexcept {
        externalPort1P3Mask_ = 0;
        externalPort1P3SelectEnabled_ = false;
    }

    void writeInternal(std::uint8_t address, std::uint8_t value) noexcept override {
        const int port = portIndex(address);
        if (port >= 0) {
            const auto index = static_cast<std::size_t>(port);
            const auto previousPins = portPins_[index];
            portLatch_[index] = value;
            portPins_[index] = value;
            const auto instructionPC = currentWriteInstructionPC(address);
            auto& portEvent = portWriteTrace_[portWriteCount_ % portWriteTrace_.size()];
            portEvent.port = static_cast<std::uint8_t>(port);
            portEvent.previousValue = previousPins;
            portEvent.value = value;
            portEvent.changedMask = static_cast<std::uint8_t>(previousPins ^ value);
            portEvent.port2 = portPins_[2];
            portEvent.port3 = portPins_[3];
            portEvent.soundBusMode = internal_[0x8e];
            portEvent.instructionPC = instructionPC;
            portEvent.eventSequence = ++traceEventSequence_;
            portEvent.machineCycle = elapsedMachineCycles_;
            ++portWriteCount_;
            if (port == 3) {
                if (((previousPins ^ value) & 0x20u) != 0)
                    ++p35TransitionCount_;
                if (isP3Bit2ControlWrite(instructionPC)) {
                    auto& controlWrite = p3Bit2ControlWriteTrace_[
                        p3Bit2ControlWriteCount_ % p3Bit2ControlWriteTrace_.size()];
                    controlWrite.instructionPC = instructionPC;
                    controlWrite.returnPC = currentStackReturnPC();
                    controlWrite.previousPort3 = previousPins;
                    controlWrite.port3 = value;
                    controlWrite.port2 = portPins_[2];
                    controlWrite.soundBusMode = internal_[0x8e];
                    controlWrite.bit2Changed =
                        ((previousPins ^ value) & 0x04u) != 0;
                    controlWrite.eventSequence = portEvent.eventSequence;
                    controlWrite.machineCycle = elapsedMachineCycles_;
                    ++p3Bit2ControlWriteCount_;
                }
                const bool wasReleased = (previousPins & 0x04u) != 0;
                const bool nowReleased = (value & 0x04u) != 0;
                if (wasReleased && !nowReleased)
                    soundDeviceResetReleased_ = false;
                else if (!wasReleased && nowReleased)
                    soundDeviceResetReleased_ = true;
            }
        } else {
            const auto previousValue = internal_[address];
            if (address == 0x36u || address == 0x37u)
                recordInternalRamWriteForTesting(
                    address, previousValue, value,
                    currentWriteInstructionPC(address), false);
            internal_[address] = value;
            if (address == 0x25u || address == 0x26u)
                recordDispatchFlagBackingWrite(address, previousValue, value);
            if (address == 0x21u && ((previousValue ^ value) & 0x04u) != 0) {
                const auto pcAfterFetch = cpu_.registers().pc;
                const auto candidatePC = static_cast<std::uint16_t>(pcAfterFetch - 2u);
                const auto candidateOpcode = readCode(candidatePC);
                const auto candidateOperand = readCode(static_cast<std::uint16_t>(candidatePC + 1u));
                const bool isFlag0AInstruction =
                    (candidateOpcode == 0xb2u || candidateOpcode == 0xc2u ||
                     candidateOpcode == 0xd2u) && candidateOperand == 0x0au;
                auto& event = internalFlagWriteTrace_[
                    internalFlagWriteTraceCount_ % internalFlagWriteTrace_.size()];
                event.byteValue = value;
                event.opcode = isFlag0AInstruction ? candidateOpcode : 0;
                event.previousValue = (previousValue & 0x04u) != 0;
                event.currentValue = (value & 0x04u) != 0;
                event.instructionPC = isFlag0AInstruction
                    ? candidatePC : static_cast<std::uint16_t>(pcAfterFetch - 1u);
                event.stackReturnPC = currentStackReturnPC();
                event.machineCycle = elapsedMachineCycles_;
                ++internalFlagWriteTraceCount_;
            }
            if (address == 0x8eu) {
                ++soundBusModeWriteCount_;
                auto& event = soundBusModeWriteTrace_[
                    soundBusModeWriteTraceCount_ % soundBusModeWriteTrace_.size()];
                event.previousValue = previousValue;
                event.value = value;
                event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
                event.returnPC = currentStackReturnPC();
                event.port2 = portPins_[2];
                event.port3 = portPins_[3];
                event.ic51Selected = isIC51Selected();
                event.eventSequence = ++traceEventSequence_;
                event.machineCycle = elapsedMachineCycles_;
                ++soundBusModeWriteTraceCount_;
            }
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
        if (address < 0x80u) {
            const auto previousValue = internal_[address];
            if (address == 0x36u || address == 0x37u)
                recordInternalRamWriteForTesting(
                    address, previousValue, value,
                    currentIndirectWriteInstructionPC(), true);
            internal_[address] = value;
            if (address == 0x25u || address == 0x26u)
                recordDispatchFlagBackingWrite(address, previousValue, value);
        } else {
            upperIram_[address - 0x80u] = value;
        }
    }

    std::uint8_t readExternal(std::uint16_t address) noexcept override {
        const auto value = readExternalImpl(address);
        recordMidiEventGateAccess(address, value, false);
        recordSlotInterpolationDataAccess(address, value, false);
        recordPitchSourceDataAccess(address, value, false);
        recordExecutionDataAccess(address, value, false);
        return value;
    }

    std::uint8_t readExternalImpl(std::uint16_t address) noexcept {
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
            if ((address >= 0x2900u && address <= 0x2bffu &&
                 address != 0x2b8bu && address != 0x2b8du) || address == 0x3098u ||
                (address >= 0x0080u && address <= 0x00ffu) ||
                (address >= 0x2800u && address <= 0x28ffu) ||
                (address >= 0x2800u && address <= 0x28ffu) ||
                (address >= 0x0100u && address <= 0x02ffu) ||
                (address >= 0x2e00u && address <= 0x2fffu) ||
                (address >= 0x3700u && address <= 0x370fu) ||
                (address >= 0x7bd4u && address <= 0x7bd7u) ||
                (address >= 0x7bd9u && address <= 0x7bdcu)) {
                auto& watch = xdataWatchTrace_[xdataWatchTraceCount_ % xdataWatchTrace_.size()];
                watch.address = address;
                watch.value = value;
                watch.write = false;
                watch.instructionPC = instructionPC;
                watch.stackReturnPC = currentStackReturnPC();
                watch.port2 = portPins_[2];
                watch.port3 = portPins_[3];
                watch.soundBusMode = internal_[0x8e];
                watch.ic51Selected = true;
                watch.writeAccepted = true;
                watch.machineCycle = elapsedMachineCycles_;
                ++xdataWatchTraceCount_;
            }
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
            event.port2 = portPins_[2];
            event.port3 = portPins_[3];
            event.soundBusMode = internal_[0x8e];
            event.ic51Selected = ic51Selected;
            event.eventSequence = ++traceEventSequence_;
            event.machineCycle = elapsedMachineCycles_;
            ++ic51ReadTraceCount_;
            return value;
        }
        if (address >= 0x8000u && address <= 0x8003u) {
            const auto index = static_cast<std::size_t>(address - 0x8000u);
            // Firmware helper 0x0DCE selects an index via register 0, then
            // reads response bytes from registers 1, 2 and 3 into R2, R3, R4.
            // Response is independent of the host's outgoing command payload.
            // No peripheral model is connected yet, so unconfigured bytes are 0xff.
            const std::uint8_t value = index == 0
                ? commandPort_[index]
                : soundHostResponseData_[soundHostCommandLatch_][index - 1u];
            auto& event = soundHostReadTrace_[
                soundHostReadCount_ % soundHostReadTrace_.size()];
            event.registerIndex = static_cast<std::uint8_t>(index);
            event.value = value;
            event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
            event.returnPC = currentStackReturnPC();
            event.machineCycle = elapsedMachineCycles_;
            event.deviceIndex = soundHostCommandLatch_;
            event.port2 = portPins_[2];
            event.port3 = portPins_[3];
            event.soundBusMode = internal_[0x8e];
            event.ic51Selected = ic51Selected;
            event.soundDeviceResetReleased = soundDeviceResetReleased_;
            event.responseInjectedForTesting = index != 0u &&
                soundHostResponseInjectedForTesting_[soundHostCommandLatch_];
            event.eventSequence = ++traceEventSequence_;
            ++soundHostReadCount_;
            return value;
        }
        if (address == 0xff10u && (internal_[0x8e] & 0x03u) != 0x02u) {
            // Preserve the real firmware's status/data-port reads so the
            // startup ready poll can be traced without inventing a response.
            auto& event = ff10ReadTrace_[
                ff10ReadCount_ % ff10ReadTrace_.size()];
            event.value = soundDeviceResponse_;
            event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
            event.stackReturnPC = currentStackReturnPC();
            event.port2 = portPins_[2];
            event.port3 = portPins_[3];
            event.soundBusMode = internal_[0x8e];
            event.ic51Selected = ic51Selected;
            event.soundDeviceResetReleased = soundDeviceResetReleased_;
            event.eventSequence = ++traceEventSequence_;
            event.machineCycle = elapsedMachineCycles_;
            ++ff10ReadCount_;
            return event.value;
        }
        for (std::size_t i = 0; i < testExternalReadOverrideEnabled_.size(); ++i) {
            if (!testExternalReadOverrideEnabled_[i] ||
                address != testExternalReadOverrideAddresses_[i])
                continue;
            const auto value = testExternalReadOverrideValues_[i];
            if (address >= 0xc000u) {
                ++soundMemoryReadCount_;
                const auto mode = static_cast<std::size_t>(internal_[0x8e] & 0x03u);
                ++soundMemoryReadsByMode_[mode];
                auto& soundEvent = soundMemoryReadTrace_[
                    soundMemoryReadTraceCount_ % soundMemoryReadTrace_.size()];
                soundEvent.address = address;
                soundEvent.instructionPC = cpu_.registers().pc;
                soundEvent.returnPC = currentStackReturnPC();
                soundEvent.value = value;
                soundEvent.mode = static_cast<std::uint8_t>(mode);
                soundEvent.port2 = portPins_[2];
                soundEvent.port3 = portPins_[3];
                soundEvent.ic51Selected = ic51Selected;
                soundEvent.eventSequence = ++traceEventSequence_;
                soundEvent.machineCycle = elapsedMachineCycles_;
                ++soundMemoryReadTraceCount_;
            }
            if ((address >= 0x7c00u && address <= 0x7fffu) || address == 0x7bfeu ||
                (address >= 0x7bd9u && address <= 0x7bdcu)) {
                auto& event = unmappedBusTrace_[unmappedBusTraceCount_ % unmappedBusTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = false;
                event.instructionPC = cpu_.registers().pc;
                event.stackReturnPC = currentStackReturnPC();
                event.port3 = portPins_[3];
                event.soundBusMode = internal_[0x8e];
                event.syntheticTestOverlay = true;
                event.machineCycle = elapsedMachineCycles_;
                ++unmappedBusTraceCount_;
            }
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
                event.port2 = portPins_[2];
                event.port3 = portPins_[3];
                event.ic51Selected = ic51Selected;
                event.eventSequence = ++traceEventSequence_;
                event.machineCycle = elapsedMachineCycles_;
                ++soundMemoryReadTraceCount_;
            }
            const auto value = xdata_[address];
            if ((address >= 0x2900u && address <= 0x2bffu &&
                 address != 0x2b8bu && address != 0x2b8du) || address == 0x3098u ||
                (address >= 0x0080u && address <= 0x00ffu) ||
                (address >= 0x0100u && address <= 0x02ffu) ||
                (address >= 0x2e00u && address <= 0x2fffu) ||
                (address >= 0x3700u && address <= 0x370fu) ||
                (address >= 0x7bd4u && address <= 0x7bd7u)) {
                auto& event = xdataWatchTrace_[xdataWatchTraceCount_ % xdataWatchTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = false;
                event.instructionPC = instructionPC;
                event.stackReturnPC = currentStackReturnPC();
                event.port2 = portPins_[2];
                event.port3 = portPins_[3];
                event.soundBusMode = internal_[0x8e];
                event.ic51Selected = ic51Selected;
                event.writeAccepted = true;
                event.machineCycle = elapsedMachineCycles_;
                ++xdataWatchTraceCount_;
            }
            return value;
        }

        if (testExternalRam7000Enabled_ && address >= 0x7000u && address <= 0x7fffu) {
            const auto value = testExternalRam7000_[address - 0x7000u];
            if (address >= 0x7c00u || address == 0x7bfeu ||
                (address >= 0x7bd9u && address <= 0x7bdcu)) {
                auto& event = unmappedBusTrace_[unmappedBusTraceCount_ % unmappedBusTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = false;
                event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
                event.stackReturnPC = currentStackReturnPC();
                event.port3 = portPins_[3];
                event.soundBusMode = internal_[0x8e];
                event.syntheticTestOverlay = true;
                event.machineCycle = elapsedMachineCycles_;
                ++unmappedBusTraceCount_;
            }
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
                    event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
                    event.stackReturnPC = currentStackReturnPC();
                    event.port3 = portPins_[3];
                    event.soundBusMode = internal_[0x8e];
                    event.machineCycle = elapsedMachineCycles_;
                    ++unmappedBusTraceCount_;
                    if (address >= 0x7000u && address <= 0x7fffu)
                        ++unmappedReadCounts7xxx_[address - 0x7000u];
                }
                return 0xff; // open bus / unmodeled hardware
        }
    }

    void writeExternal(std::uint16_t address, std::uint8_t value) noexcept override {
        writeExternalImpl(address, value);
        recordMidiEventGateAccess(address, value, true);
        recordSlotInterpolationDataAccess(address, value, true);
        recordPitchSourceDataAccess(address, value, true);
        recordExecutionDataAccess(address, value, true);
    }

    void writeExternalImpl(std::uint16_t address, std::uint8_t value) noexcept {
        const auto instructionPC =
            static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
        // FF10 can appear both in its dedicated aperture trace and in the
        // external-memory aperture classification. Keep one sequence ID for
        // that single MOVX operation instead of inventing two bus events.
        // Keep exact writes from the interpreter's four-byte nested-record
        // copier even if the broad 8192-entry XDATA ring wraps later in the test.
        std::uint64_t accessSequence = 0;
        const bool nestedResourceWritePC =
            instructionPC == 0x1a32u || instructionPC == 0x1a35u ||
            instructionPC == 0x1a38u || instructionPC == 0x1a3cu ||
            instructionPC == 0x1a44u || instructionPC == 0x1a57u ||
            instructionPC == 0x1a82u || instructionPC == 0x1a88u ||
            instructionPC == 0x1a9bu || instructionPC == 0x1a9eu ||
            instructionPC == 0x1aa5u ||
            instructionPC == 0x1c9cu || instructionPC == 0x1cabu ||
            instructionPC == 0x1cb1u || instructionPC == 0x1cb4u ||
            instructionPC == 0x1cb7u || instructionPC == 0x1cbfu;
        if (nestedResourceWritePC &&
            address >= 0x0100u && address <= 0x02ffu) {
            auto& event = resourceTupleWriteTrace_[
                resourceTupleWriteTraceCount_ % resourceTupleWriteTrace_.size()];
            event.address = address;
            event.value = value;
            event.instructionPC = instructionPC;
            event.stackReturnPC = currentStackReturnPC();
            event.port2 = portPins_[2];
            event.port3 = portPins_[3];
            event.machineCycle = elapsedMachineCycles_;
            ++resourceTupleWriteTraceCount_;
        }
        const bool ramMode = (internal_[0x8e] & 0x03u) == 0x02u;
        const bool ic51Selected = isIC51Selected();
        if (address == 0xff10u) {
            // FF10 is a mode-dependent aperture: status/data-port traffic in
            // mode 0/1, external sound-memory traffic in mode 2. Record both.
            auto& event = ff10WriteTrace_[
                ff10WriteCount_ % ff10WriteTrace_.size()];
            event.value = value;
            event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
            event.stackReturnPC = currentStackReturnPC();
            event.port2 = portPins_[2];
            event.port3 = portPins_[3];
            event.soundBusMode = internal_[0x8e];
            event.ic51Selected = ic51Selected;
            event.soundDeviceResetReleased = soundDeviceResetReleased_;
            accessSequence = ++traceEventSequence_;
            event.eventSequence = accessSequence;
            event.machineCycle = elapsedMachineCycles_;
            ++ff10WriteCount_;
        }
        if (ic51Selected && !isDedicatedPeripheralAddress(address) &&
            !(ramMode && isXdataRam(address))) {
            if ((address >= 0x2000u && address <= 0x23ffu) ||
                (address >= 0x2900u && address <= 0x2bffu &&
                 address != 0x2b8bu && address != 0x2b8du) || address == 0x3098u ||
                (address >= 0x0080u && address <= 0x00ffu) ||
                (address >= 0x2800u && address <= 0x28ffu) ||
                (address >= 0x2800u && address <= 0x28ffu) ||
                (address >= 0x0100u && address <= 0x02ffu) ||
                (address >= 0x2e00u && address <= 0x2fffu) ||
                (address >= 0x3700u && address <= 0x370fu) || address == 0x3098u ||
                (address >= 0x7bd4u && address <= 0x7bd7u)) {
                auto& event = xdataWatchTrace_[xdataWatchTraceCount_ % xdataWatchTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = true;
                event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
                event.stackReturnPC = currentStackReturnPC();
                event.port2 = portPins_[2];
                event.port3 = portPins_[3];
                event.soundBusMode = internal_[0x8e];
                event.ic51Selected = true;
                event.writeAccepted = false;
                event.machineCycle = elapsedMachineCycles_;
                ++xdataWatchTraceCount_;
            }
            return; // IC51 is read-only; dedicated I/O and RAM remain writable.
        }
        if (address == 0xff10u && !ramMode) {
            soundDeviceWrite_ = value; // status-port traffic outside RAM mode
            return;
        }
        if (isXdataRam(address)) {
            xdata_[address] = value;
            if ((address >= 0x2000u && address <= 0x23ffu) ||
                (address >= 0x2900u && address <= 0x2bffu &&
                 address != 0x2b8bu && address != 0x2b8du) || address == 0x3098u ||
                (address >= 0x0080u && address <= 0x00ffu) ||
                (address >= 0x0100u && address <= 0x02ffu) ||
                (address >= 0x2e00u && address <= 0x2fffu) ||
                (address >= 0x3700u && address <= 0x370fu) || address == 0x3098u ||
                (address >= 0x7bd4u && address <= 0x7bd7u)) {
                auto& event = xdataWatchTrace_[xdataWatchTraceCount_ % xdataWatchTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = true;
                event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
                event.stackReturnPC = currentStackReturnPC();
                event.port2 = portPins_[2];
                event.port3 = portPins_[3];
                event.soundBusMode = internal_[0x8e];
                event.ic51Selected = ic51Selected;
                event.writeAccepted = true;
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
                event.port2 = portPins_[2];
                event.port3 = portPins_[3];
                event.ic51Selected = ic51Selected;
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
                event.eventSequence = accessSequence != 0u
                    ? accessSequence : ++traceEventSequence_;
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

            auto& byteContext = soundHostTransactionContext_[index];
            byteContext.instructionPC = instructionPC;
            byteContext.machineCycle = elapsedMachineCycles_;
            byteContext.deviceIndex = soundHostCommandLatch_;
            byteContext.port2 = portPins_[2];
            byteContext.port3 = portPins_[3];
            byteContext.soundBusMode = internal_[0x8e];
            byteContext.ic51Selected = isIC51Selected();
            byteContext.soundDeviceResetReleased = soundDeviceResetReleased_;

            auto& hostWrite = soundHostWriteTrace_[
                soundHostWriteCount_ % soundHostWriteTrace_.size()];
            hostWrite.registerIndex = static_cast<std::uint8_t>(index);
            hostWrite.value = value;
            hostWrite.instructionPC = instructionPC;
            hostWrite.returnPC = currentStackReturnPC();
            hostWrite.machineCycle = elapsedMachineCycles_;
            hostWrite.deviceIndex = soundHostCommandLatch_;
            hostWrite.port2 = portPins_[2];
            hostWrite.port3 = portPins_[3];
            hostWrite.soundBusMode = internal_[0x8e];
            hostWrite.ic51Selected = isIC51Selected();
            hostWrite.soundDeviceResetReleased = soundDeviceResetReleased_;
            hostWrite.eventSequence = ++traceEventSequence_;
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
                    packet.byteContext = soundHostTransactionContext_;
                    packet.instructionPC = instructionPC;
                    const auto stackPointer = cpu_.registers().sp;
                    const auto returnHigh = readIndirect(stackPointer);
                    const auto returnLow = readIndirect(static_cast<std::uint8_t>(stackPointer - 1u));
                    packet.returnPC = static_cast<std::uint16_t>(
                        (static_cast<std::uint16_t>(returnHigh) << 8u) | returnLow);
                    packet.machineCycle = elapsedMachineCycles_;
                    // Packet-level context is the first byte's bus state;
                    // byteContext retains any transitions across the four writes.
                    packet.deviceIndex = packet.byteContext[0].deviceIndex;
                    packet.port2 = packet.byteContext[0].port2;
                    packet.port3 = packet.byteContext[0].port3;
                    packet.soundBusMode = packet.byteContext[0].soundBusMode;
                    packet.ic51Selected = packet.byteContext[0].ic51Selected;
                    packet.soundDeviceResetReleased =
                        packet.byteContext[0].soundDeviceResetReleased;
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
            if (address >= 0x7c00u || address == 0x7bfeu) {
                auto& event = unmappedBusTrace_[unmappedBusTraceCount_ % unmappedBusTrace_.size()];
                event.address = address;
                event.value = value;
                event.write = true;
                event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
                event.stackReturnPC = currentStackReturnPC();
                event.port3 = portPins_[3];
                event.soundBusMode = internal_[0x8e];
                event.syntheticTestOverlay = true;
                event.machineCycle = elapsedMachineCycles_;
                ++unmappedBusTraceCount_;
            }
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
                    event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
                    event.stackReturnPC = currentStackReturnPC();
                    event.port3 = portPins_[3];
                    event.soundBusMode = internal_[0x8e];
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
    std::size_t p3Bit2ControlWriteCount() const noexcept {
        return p3Bit2ControlWriteCount_;
    }
    const P3Bit2ControlWriteEvent& p3Bit2ControlWriteAt(std::size_t index) const noexcept {
        return p3Bit2ControlWriteTrace_[index % p3Bit2ControlWriteTrace_.size()];
    }
    std::size_t portReadCount() const noexcept { return portReadCount_; }
    const PortReadEvent& portReadAt(std::size_t index) const noexcept {
        return portReadTrace_[index % portReadTrace_.size()];
    }

    std::uint8_t lastSoundDeviceWrite() const noexcept { return soundDeviceWrite_; }
    std::size_t ff10ReadCount() const noexcept { return ff10ReadCount_; }
    const FF10ReadEvent& ff10ReadAt(std::size_t index) const noexcept {
        return ff10ReadTrace_[index % ff10ReadTrace_.size()];
    }
    std::size_t ff10WriteCount() const noexcept {
        return ff10WriteCount_;
    }
    const FF10WriteEvent& ff10WriteAt(std::size_t index) const noexcept {
        return ff10WriteTrace_[index % ff10WriteTrace_.size()];
    }
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
    void clearInternalRamWriteTraceForTesting() noexcept {
        internalRamWriteTrace_.fill(InternalRamWriteEvent{});
        internalRamWriteTraceCount_ = 0;
    }
    std::size_t internalRamWriteTraceCountForTesting() const noexcept {
        return internalRamWriteTraceCount_;
    }
    const InternalRamWriteEvent& internalRamWriteTraceAtForTesting(
        std::size_t index) const noexcept {
        return internalRamWriteTrace_[index % internalRamWriteTrace_.size()];
    }
    std::uint64_t soundBusModeWriteCount() const noexcept { return soundBusModeWriteCount_; }
    std::size_t soundBusModeWriteTraceCount() const noexcept {
        return soundBusModeWriteTraceCount_;
    }
    const SoundBusModeWriteEvent& soundBusModeWriteTraceAt(std::size_t index) const noexcept {
        return soundBusModeWriteTrace_[index % soundBusModeWriteTrace_.size()];
    }
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
    void setTestInternalWriteOnCall(std::uint16_t callTarget,
                                     std::uint8_t address,
                                     std::uint8_t value) noexcept {
        testInternalWriteOnCallTarget_ = callTarget;
        testInternalWriteOnCallAddress_ = address;
        testInternalWriteOnCallValue_ = value;
        testInternalWriteOnCallEnabled_ = true;
    }

    void setTestExternalReadOverride(std::uint16_t address, std::uint8_t value) noexcept {
        std::size_t freeSlot = testExternalReadOverrideEnabled_.size();
        for (std::size_t i = 0; i < testExternalReadOverrideEnabled_.size(); ++i) {
            if (testExternalReadOverrideEnabled_[i] &&
                testExternalReadOverrideAddresses_[i] == address) {
                testExternalReadOverrideValues_[i] = value;
                return;
            }
            if (!testExternalReadOverrideEnabled_[i] &&
                freeSlot == testExternalReadOverrideEnabled_.size())
                freeSlot = i;
        }
        if (freeSlot == testExternalReadOverrideEnabled_.size())
            freeSlot = 0;
        testExternalReadOverrideAddresses_[freeSlot] = address;
        testExternalReadOverrideValues_[freeSlot] = value;
        testExternalReadOverrideEnabled_[freeSlot] = true;
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
    void recordIndexedPairMathWatch(std::uint16_t pc) noexcept {
        const auto& registers = cpu_.registers();
        auto& event = indexedPairMathWatchTrace_[
            indexedPairMathWatchCount_ % indexedPairMathWatchTrace_.size()];
        event.instructionPC = pc;
        event.returnPC = currentStackReturnPC();
        event.dptr = registers.dptr;
        event.accumulator = registers.a;
        event.b = registers.b;
        event.psw = registers.psw.pack();
        const auto bankBase = registers.psw.registerBankBase();
        for (std::size_t index = 0; index < event.registers.size(); ++index)
            event.registers[index] = readInternal(
                static_cast<std::uint8_t>(bankBase + index));
        event.ram28 = readInternal(0x28u);
        event.ram29 = readInternal(0x29u);
        event.ram2A = readInternal(0x2au);
        event.ram6B = readInternal(0x6bu);
        event.ram6C = readInternal(0x6cu);
        event.ram6E = readInternal(0x6eu);
        event.ram6F = readInternal(0x6fu);
        event.ram70 = readInternal(0x70u);
        event.ram71 = readInternal(0x71u);
        event.machineCycle = elapsedMachineCycles_;
        ++indexedPairMathWatchCount_;
    }

    void recordMidiEventGateAccess(std::uint16_t address, std::uint8_t value,
                                   bool write) noexcept {
        if (!((address >= 0x30e4u && address <= 0x30e7u) ||
              address == 0x30f0u))
            return;
        auto& event = midiEventGateTrace_[
            midiEventGateTraceCount_ % midiEventGateTrace_.size()];
        event.address = address;
        event.value = value;
        event.write = write;
        event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
        event.stackReturnPC = currentStackReturnPC();
        event.soundBusMode = internal_[0x8e];
        event.ic51Selected = isIC51Selected();
        event.machineCycle = elapsedMachineCycles_;
        ++midiEventGateTraceCount_;
    }

    void recordDispatchFlagBackingWrite(std::uint8_t address,
                                        std::uint8_t previousValue,
                                        std::uint8_t currentValue) noexcept {
        const auto pcAfterFetch = cpu_.registers().pc;
        const auto instructionPC = static_cast<std::uint16_t>(pcAfterFetch - 2u);
        auto& event = dispatchFlagWriteTrace_[
            dispatchFlagWriteTraceCount_ % dispatchFlagWriteTrace_.size()];
        event.address = address;
        event.previousValue = previousValue;
        event.currentValue = currentValue;
        event.instructionPC = instructionPC;
        event.opcode = readCode(instructionPC);
        event.operand = readCode(static_cast<std::uint16_t>(instructionPC + 1u));
        event.stackReturnPC = currentStackReturnPC();
        event.machineCycle = elapsedMachineCycles_;
        ++dispatchFlagWriteTraceCount_;
    }

    void recordSlotInterpolationDataAccess(std::uint16_t address,
                                            std::uint8_t value,
                                            bool write) noexcept {
        const auto pc = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
        if (pc < 0x09b9u || pc > 0x0a3du)
            return;
        auto& event = slotInterpolationDataAccessTrace_[
            slotInterpolationDataAccessTraceCount_ %
                slotInterpolationDataAccessTrace_.size()];
        event.address = address;
        event.instructionPC = pc;
        event.stackReturnPC = currentStackReturnPC();
        event.value = value;
        event.port2 = portLatch_[2];
        event.port3 = portPins_[3];
        event.soundBusMode = internal_[0x8e];
        event.write = write;
        event.ic51Selected = isIC51Selected();
        event.machineCycle = elapsedMachineCycles_;
        ++slotInterpolationDataAccessTraceCount_;
    }

    void recordPitchSourceDataAccess(std::uint16_t address,
                                      std::uint8_t value,
                                      bool write) noexcept {
        const bool indexedCurveSourceAddress =
            address >= 0x7c29u && address <= 0x7d91u &&
            ((address - 0x7c29u) % 0x18u) == 0u;
        const bool pitchSourceAddress =
            (address >= 0x0081u && address <= 0x0083u) ||
            (address >= 0x0091u && address <= 0x0093u) ||
            (address >= 0x2000u && address <= 0x201fu) ||
            (address >= 0x2054u && address <= 0x2063u) ||
            indexedCurveSourceAddress;
        if (!pitchSourceAddress)
            return;
        auto& event = pitchSourceDataAccessTrace_[
            pitchSourceDataAccessTraceCount_ % pitchSourceDataAccessTrace_.size()];
        event.address = address;
        event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
        event.stackReturnPC = currentStackReturnPC();
        event.value = value;
        event.port2 = portLatch_[2];
        event.port3 = portPins_[3];
        event.soundBusMode = internal_[0x8e];
        event.write = write;
        event.ic51Selected = isIC51Selected();
        event.machineCycle = elapsedMachineCycles_;
        ++pitchSourceDataAccessTraceCount_;
    }

    void recordExecutionDataAccess(std::uint16_t address, std::uint8_t value,
                                   bool write) noexcept {
        if (!executionTraceActive_)
            return;
        auto& event = executionDataAccessTrace_[
            executionDataAccessTraceCount_ % executionDataAccessTrace_.size()];
        event.instructionPC = static_cast<std::uint16_t>(cpu_.registers().pc - 1u);
        event.stackReturnPC = currentStackReturnPC();
        event.address = address;
        event.machineCycle = elapsedMachineCycles_;
        event.value = value;
        event.port2 = portPins_[2];
        event.port3 = portPins_[3];
        event.soundBusMode = internal_[0x8e];
        event.write = write;
        event.ic51Selected = isIC51Selected();
        ++executionDataAccessTraceCount_;
    }

    bool isP3Bit2ControlWrite(std::uint16_t instructionPC) const noexcept {
        const auto opcode = readCode(instructionPC);
        const auto operand = readCode(static_cast<std::uint16_t>(instructionPC + 1u));
        const auto third = readCode(static_cast<std::uint16_t>(instructionPC + 2u));
        const bool bitWrite =
            (opcode == 0x10u || opcode == 0x92u || opcode == 0xb2u ||
             opcode == 0xc2u || opcode == 0xd2u) && operand == 0xb2u;
        const bool wholePortWrite =
            (opcode == 0x75u && operand == 0xb0u) ||
            (opcode == 0xf5u && operand == 0xb0u) ||
            (opcode == 0x85u && third == 0xb0u) ||
            ((opcode == 0x43u || opcode == 0x53u || opcode == 0x63u ||
              opcode == 0x42u || opcode == 0x52u || opcode == 0x62u ||
              opcode == 0x05u || opcode == 0x15u || opcode == 0xc5u ||
              opcode == 0xd0u || opcode == 0xd5u ||
              (opcode >= 0x86u && opcode <= 0x8fu)) && operand == 0xb0u);
        return bitWrite || wholePortWrite;
    }

    std::uint16_t currentWriteInstructionPC(std::uint8_t directAddress) const noexcept {
        const auto pcAfterInstructionFetch = cpu_.registers().pc;
        const auto twoBytePC = static_cast<std::uint16_t>(pcAfterInstructionFetch - 2u);
        const auto threeBytePC = static_cast<std::uint16_t>(pcAfterInstructionFetch - 3u);
        const auto threeByteOpcode = readCode(threeBytePC);
        const auto secondByte = readCode(static_cast<std::uint16_t>(threeBytePC + 1u));
        const auto thirdByte = readCode(static_cast<std::uint16_t>(threeBytePC + 2u));
        // P3 writes in IC4 include MOV direct,#data, MOV direct,direct and
        // the direct-immediate logical operations. Most bit-address writes
        // are two-byte instructions and fall through to twoBytePC.
        if ((threeByteOpcode == 0x75u && secondByte == directAddress) ||
            (threeByteOpcode == 0x85u && thirdByte == directAddress) ||
            ((threeByteOpcode == 0x43u || threeByteOpcode == 0x53u ||
              threeByteOpcode == 0x63u) && secondByte == directAddress))
            return threeBytePC;
        return twoBytePC;
    }

    std::uint16_t currentStackReturnPC() const noexcept {
        const auto stackPointer = cpu_.registers().sp;
        const auto returnHigh = readIndirect(stackPointer);
        const auto returnLow = readIndirect(static_cast<std::uint8_t>(stackPointer - 1u));
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(returnHigh) << 8u) | returnLow);
    }

    std::uint16_t currentIndirectWriteInstructionPC() const noexcept {
        const auto pcAfterFetch = cpu_.registers().pc;
        const auto oneBytePC = static_cast<std::uint16_t>(pcAfterFetch - 1u);
        const auto oneByteOpcode = readCode(oneBytePC);
        // These indirect-Ri destination instructions are one byte long.
        if (oneByteOpcode == 0x06u || oneByteOpcode == 0x07u ||
            oneByteOpcode == 0x16u || oneByteOpcode == 0x17u ||
            oneByteOpcode == 0x46u || oneByteOpcode == 0x47u ||
            oneByteOpcode == 0x56u || oneByteOpcode == 0x57u ||
            oneByteOpcode == 0x66u || oneByteOpcode == 0x67u ||
            oneByteOpcode == 0xc6u || oneByteOpcode == 0xc7u ||
            oneByteOpcode == 0xd6u || oneByteOpcode == 0xd7u ||
            oneByteOpcode == 0xf6u || oneByteOpcode == 0xf7u)
            return oneBytePC;

        const auto twoBytePC = static_cast<std::uint16_t>(pcAfterFetch - 2u);
        const auto twoByteOpcode = readCode(twoBytePC);
        // MOV @Ri,#data and MOV @Ri,direct consume one operand byte.
        if (twoByteOpcode == 0x76u || twoByteOpcode == 0x77u ||
            twoByteOpcode == 0xa6u || twoByteOpcode == 0xa7u)
            return twoBytePC;
        return oneBytePC;
    }

    void recordInternalRamWriteForTesting(
        std::uint8_t address, std::uint8_t previousValue, std::uint8_t value,
        std::uint16_t instructionPC, bool indirectAddressing) noexcept {
        const auto& registers = cpu_.registers();
        auto& event = internalRamWriteTrace_[
            internalRamWriteTraceCount_ % internalRamWriteTrace_.size()];
        event.address = address;
        event.previousValue = previousValue;
        event.value = value;
        event.indirectAddressing = indirectAddressing;
        event.instructionPC = instructionPC;
        event.pcAfterFetch = registers.pc;
        event.returnPC = currentStackReturnPC();
        event.opcodeBytes = {{
            readCode(instructionPC),
            readCode(static_cast<std::uint16_t>(instructionPC + 1u)),
            readCode(static_cast<std::uint16_t>(instructionPC + 2u))
        }};
        event.dptr = registers.dptr;
        event.accumulator = registers.a;
        event.b = registers.b;
        event.psw = registers.psw.pack();
        event.registerBankBase = registers.psw.registerBankBase();
        for (std::size_t i = 0; i < event.registers.size(); ++i)
            event.registers[i] = readInternal(static_cast<std::uint8_t>(
                event.registerBankBase + i));
        event.port2 = portPins_[2];
        event.port3 = portPins_[3];
        event.soundBusMode = internal_[0x8e];
        event.eventSequence = ++traceEventSequence_;
        event.machineCycle = elapsedMachineCycles_;
        ++internalRamWriteTraceCount_;
    }

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
    // Retain a complete single MIDI-event IC51 trace (about 14.5k reads in the regression).
    std::array<IC51ReadEvent, 16384> ic51ReadTrace_{};
    std::size_t ic51ReadTraceCount_ = 0;
    std::array<InternalFlagWriteEvent, 256> internalFlagWriteTrace_{};
    std::size_t internalFlagWriteTraceCount_ = 0;
    std::array<BulkTransferEntryEvent, 16> bulkTransferEntryTrace_{};
    std::size_t bulkTransferEntryCount_ = 0;
    std::array<InstructionWatchEvent, 1024> instructionWatchTrace_{};
    std::size_t instructionWatchCount_ = 0;
    std::array<IndexedPairMathWatchEvent, 1024> indexedPairMathWatchTrace_{};
    std::size_t indexedPairMathWatchCount_ = 0;
    std::array<SlotInterpolationDataAccessEvent, 256> slotInterpolationDataAccessTrace_{};
    std::size_t slotInterpolationDataAccessTraceCount_ = 0;
    std::array<SlotInterpolationDataAccessEvent, 2048> pitchSourceDataAccessTrace_{};
    std::size_t pitchSourceDataAccessTraceCount_ = 0;
    std::array<ExecutionInstructionEvent, 4096> executionInstructionTrace_{};
    std::size_t executionInstructionTraceCount_ = 0;
    std::array<ExecutionDataAccessEvent, 8192> executionDataAccessTrace_{};
    std::size_t executionDataAccessTraceCount_ = 0;
    std::uint16_t executionTraceWindowStartPC_ = 0;
    bool executionTraceArmed_ = false;
    bool executionTraceActive_ = false;
    std::array<XdataWatchEvent, 8192> xdataWatchTrace_{};
    std::size_t xdataWatchTraceCount_ = 0;
    std::array<ResourceTupleWriteEvent, 128> resourceTupleWriteTrace_{};
    std::size_t resourceTupleWriteTraceCount_ = 0;
    std::array<MidiEventGateAccessEvent, 256> midiEventGateTrace_{};
    std::size_t midiEventGateTraceCount_ = 0;
    std::array<DispatchFlagBackingWriteEvent, 256> dispatchFlagWriteTrace_{};
    std::size_t dispatchFlagWriteTraceCount_ = 0;
    std::array<UnmappedBusEvent, 4096> unmappedBusTrace_{};
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
    std::uint8_t externalPort1P3Mask_ = 0;
    std::uint8_t externalPort1P3LowValue_ = 0xff;
    std::uint8_t externalPort1P3HighValue_ = 0xff;
    bool externalPort1P3SelectEnabled_ = false;
    std::array<std::uint8_t, 4> commandPort_{};
    std::uint8_t soundHostCommandLatch_ = 0;
    std::array<std::uint16_t, 256> soundHostWords_{};
    std::array<std::array<std::uint8_t, 3>, 256> soundHostData_{};
    std::array<std::array<std::uint8_t, 3>, 256> soundHostResponseData_{};
    std::array<bool, 256> soundHostResponseInjectedForTesting_{};
    std::array<SoundCommandPacket, 1024> soundCommandTrace_{};
    std::size_t soundCommandPacketCount_ = 0;
    std::array<soundHostReadEvent, 2048> soundHostReadTrace_{};
    std::size_t soundHostReadCount_ = 0;
    std::array<soundHostWriteEvent, 4096> soundHostWriteTrace_{};
    std::size_t soundHostWriteCount_ = 0;
    bool soundHostTransactionActive_ = false;
    std::uint8_t soundHostExpectedRegister_ = 0;
    std::array<std::uint8_t, 4> soundHostTransactionBytes_{};
    std::array<SoundHostBusContext, 4> soundHostTransactionContext_{};
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
    std::array<PortWriteEvent, 4096> portWriteTrace_{};
    std::size_t portWriteCount_ = 0;
    std::array<SoundBusModeWriteEvent, 512> soundBusModeWriteTrace_{};
    std::size_t soundBusModeWriteTraceCount_ = 0;
    std::array<InternalRamWriteEvent, 512> internalRamWriteTrace_{};
    std::size_t internalRamWriteTraceCount_ = 0;
    std::array<P3Bit2ControlWriteEvent, 128> p3Bit2ControlWriteTrace_{};
    std::size_t p3Bit2ControlWriteCount_ = 0;
    mutable std::array<PortReadEvent, 512> portReadTrace_{};
    mutable std::size_t portReadCount_ = 0;
    std::array<std::uint64_t, 24> watchedCallCounts_{};
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
    std::uint64_t traceEventSequence_ = 0;
    bool soundDeviceResetReleased_ = true;
    std::uint8_t soundDeviceWrite_ = 0;
    std::array<FF10ReadEvent, 4096> ff10ReadTrace_{};
    std::size_t ff10ReadCount_ = 0;
    std::array<FF10WriteEvent, 4096> ff10WriteTrace_{};
    std::size_t ff10WriteCount_ = 0;
    std::uint8_t soundDeviceResponse_ = 0xff;
    bool testExternalRam7000Enabled_ = false;
    std::array<std::uint8_t, 0x1000> testExternalRam7000_{};
    std::array<ExternalWindowEvent, 1024> externalWindowTrace_{};
    std::size_t externalWindowTraceCount_ = 0;
    std::array<bool, 8> testExternalReadOverrideEnabled_{};
    std::array<std::uint16_t, 8> testExternalReadOverrideAddresses_{};
    std::array<std::uint8_t, 8> testExternalReadOverrideValues_{{0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu}};
    bool testInternalWriteOnCallEnabled_ = false;
    std::uint16_t testInternalWriteOnCallTarget_ = 0;
    std::uint8_t testInternalWriteOnCallAddress_ = 0;
    std::uint8_t testInternalWriteOnCallValue_ = 0;
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
