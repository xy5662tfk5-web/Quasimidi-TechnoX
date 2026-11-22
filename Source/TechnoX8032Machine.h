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

class TechnoX8032Machine final : public MCS51Bus {
public:
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

    static constexpr std::size_t kIC4RomSize = 0x20000;
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

    void reset() noexcept {
        cpu_.reset();
        timers_.reset();
        internal_.fill(0);
        upperIram_.fill(0);
        xdata_.fill(0);
        portLatch_.fill(0xff);
        portPins_.fill(0xff);
        commandPort_.fill(0);
        aciaControl_ = 0;
        aciaStatus_ = 0x02; // TDRE
        aciaReceive_ = 0;
        aciaTransmit_ = 0;
        aciaTransmitPending_ = false;
        lcdStatus_ = 0;
        lcdData_ = 0;
        lcdInstruction_ = 0;
        elapsedMachineCycles_ = 0;
        soundDeviceResetReleased_ = true;
        soundDeviceBootRequested_ = false;
        soundDeviceDataLoaded_ = false;
        soundDeviceReady_ = false;
        cpuFaulted_ = false;
        scheduledAciaHead_ = 0;
        scheduledAciaTail_ = 0;
        scheduledAciaCount_ = 0;
        nextAciaReceiveCycle_ = 0;
        internal_[0x81] = 0x07; // SP reset value
    }

    bool romLoaded() const noexcept { return romLoaded_; }
    const MCS51Core& cpu() const noexcept { return cpu_; }
    MCS51Core& cpuForTesting() noexcept { return cpu_; }
    std::uint64_t elapsedMachineCycles() const noexcept { return elapsedMachineCycles_; }

    MCS51StepResult step() noexcept { return cpu_.step(*this); }

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
        return port >= 0 ? portPins_[static_cast<std::size_t>(port)] : internal_[address];
    }

    void writeInternal(std::uint8_t address, std::uint8_t value) noexcept override {
        const int port = portIndex(address);
        if (port >= 0) {
            const auto index = static_cast<std::size_t>(port);
            const auto previousPins = portPins_[index];
            portLatch_[index] = value;
            portPins_[index] = value;
            if (port == 3) {
                const bool wasReleased = (previousPins & 0x04u) != 0;
                const bool nowReleased = (value & 0x04u) != 0;
                if (wasReleased && !nowReleased) {
                    soundDeviceResetReleased_ = false;
                    soundDeviceBootRequested_ = false;
                    soundDeviceDataLoaded_ = false;
                    soundDeviceReady_ = false;
                } else if (!wasReleased && nowReleased) {
                    soundDeviceResetReleased_ = true;
                    soundDeviceReady_ = soundDeviceBootRequested_ && soundDeviceDataLoaded_;
                }
            }
        } else {
            internal_[address] = value;
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
        if (address == 0xff10u && soundDeviceReady_)
            return 0xacu; // IC4 expects the external sound block's ready signature
        if (isXdataRam(address))
            return xdata_[address];

        switch (address) {
            case 0xa002: return aciaStatus_;
            case 0xa003: {
                const auto value = aciaReceive_;
                aciaStatus_ = static_cast<std::uint8_t>(aciaStatus_ & ~0x21u); // clear RDRF/OVRN
                refreshAciaInterrupt();
                return value;
            }
            case 0xb002: return lcdStatus_;
            case 0xb003: return lcdData_;
            default: return 0xff; // open bus / unmodeled hardware
        }
    }

    void writeExternal(std::uint16_t address, std::uint8_t value) noexcept override {
        if (address == 0xff10u) {
            xdata_[address] = value;
            if (!soundDeviceResetReleased_ && value == 0xffu)
                soundDeviceBootRequested_ = true;
            return;
        }
        if (isXdataRam(address)) {
            xdata_[address] = value;
            if (address >= 0xc000u && address != 0xff10u &&
                !soundDeviceResetReleased_ && soundDeviceBootRequested_)
                soundDeviceDataLoaded_ = true;
            return;
        }

        if (address >= 0x8000u && address <= 0x8003u) {
            commandPort_[address - 0x8000u] = value;
            return;
        }

        switch (address) {
            case 0xa000:
                aciaControl_ = value;
                if ((value & 0x03u) == 0x03u)
                    aciaStatus_ = 0x02;
                refreshAciaInterrupt();
                break;
            case 0xa001:
                aciaTransmit_ = value;
                aciaTransmitPending_ = true;
                aciaStatus_ = static_cast<std::uint8_t>(aciaStatus_ | 0x02u);
                break;
            case 0xb000: lcdInstruction_ = value; break;
            case 0xb001: lcdData_ = value; break;
            default: break; // writes to unmapped/stub regions are ignored
        }
    }

    void advanceMachineCycles(unsigned cycles) noexcept override {
        for (unsigned cycle = 0; cycle < cycles; ++cycle) {
            ++elapsedMachineCycles_;
            deliverScheduledAciaBytes();
            timers_.advanceMachineCycles(*this, 1);
        }
    }

    void setPortPins(std::uint8_t portAddress, std::uint8_t value) noexcept {
        const int port = portIndex(portAddress);
        if (port >= 0) portPins_[static_cast<std::size_t>(port)] = value;
    }

    void injectAciaReceiveByte(std::uint8_t value) noexcept {
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

    bool soundDeviceBootRequested() const noexcept { return soundDeviceBootRequested_; }
    bool soundDeviceDataLoaded() const noexcept { return soundDeviceDataLoaded_; }
    bool soundDeviceReady() const noexcept { return soundDeviceReady_; }

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

    static constexpr bool isXdataRam(std::uint16_t address) noexcept {
        // 0000-38FF is MAME's mapped RAM. C000-FFFF is a provisional
        // storage-like model required by IC4's full 16-KiB write/read test.
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
        // ACIA IRQ is wired to the 8032 INT1 input in the published MAME map.
        const bool irqActive = (aciaControl_ & 0x80u) != 0 && (aciaStatus_ & 0x01u) != 0;
        if (irqActive)
            portPins_[3] = static_cast<std::uint8_t>(portPins_[3] & ~0x08u); // INT1 active low
        else
            portPins_[3] = static_cast<std::uint8_t>(portPins_[3] | 0x08u);
    }

    std::uint32_t oscillatorHz_ = kAssumedOscillatorHz;
    MCS51Core cpu_{};
    MCS51TimerUnit timers_{};
    std::array<std::uint8_t, kIC4RomSize> rom_{};
    std::array<std::uint8_t, kCodeSpaceSize> code_{};
    std::array<std::uint8_t, 0x100> internal_{};
    std::array<std::uint8_t, 0x80> upperIram_{};
    std::array<std::uint8_t, 0x10000> xdata_{};
    std::array<std::uint8_t, 4> portLatch_{{0xff, 0xff, 0xff, 0xff}};
    std::array<std::uint8_t, 4> portPins_{{0xff, 0xff, 0xff, 0xff}};
    std::array<std::uint8_t, 4> commandPort_{};
    std::uint8_t aciaControl_ = 0;
    std::uint8_t aciaStatus_ = 0x02;
    std::uint8_t aciaReceive_ = 0;
    std::uint8_t aciaTransmit_ = 0;
    std::array<ScheduledAciaByte, 4096> scheduledAciaBytes_{};
    std::size_t scheduledAciaHead_ = 0;
    std::size_t scheduledAciaTail_ = 0;
    std::size_t scheduledAciaCount_ = 0;
    std::uint64_t nextAciaReceiveCycle_ = 0;
    bool aciaTransmitPending_ = false;
    std::uint8_t lcdStatus_ = 0;
    std::uint8_t lcdData_ = 0;
    std::uint8_t lcdInstruction_ = 0;
    std::uint64_t elapsedMachineCycles_ = 0;
    bool soundDeviceResetReleased_ = true;
    bool soundDeviceBootRequested_ = false;
    bool soundDeviceDataLoaded_ = false;
    bool soundDeviceReady_ = false;
    bool cpuFaulted_ = false;
    bool romLoaded_ = false;
};

} // namespace technox
