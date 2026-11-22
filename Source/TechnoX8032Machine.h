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
            if (step() == MCS51StepResult::unsupportedOpcode)
                break;
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
            portLatch_[index] = value;
            portPins_[index] = value;
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
        if (isXdataRam(address)) {
            xdata_[address] = value;
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
        elapsedMachineCycles_ += cycles;
        timers_.advanceMachineCycles(*this, cycles);
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

    bool takeAciaTransmitByte(std::uint8_t& value) noexcept {
        if (!aciaTransmitPending_) return false;
        value = aciaTransmit_;
        aciaTransmitPending_ = false;
        return true;
    }

    const std::array<std::uint8_t, 4>& commandPortBytes() const noexcept {
        return commandPort_;
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

    static constexpr bool isXdataRam(std::uint16_t address) noexcept {
        // 0000-38FF is MAME's mapped RAM. C000-FFFF is a provisional
        // storage-like model required by IC4's full 16-KiB write/read test.
        return address <= 0x38ffu || address >= 0xc000u;
    }

    void refreshAciaInterrupt() noexcept {
        // ACIA IRQ is wired to the 8032 INT1 input in the published MAME map.
        auto tcon = internal_[0x88];
        if ((aciaControl_ & 0x80u) != 0 && (aciaStatus_ & 0x01u) != 0)
            tcon = static_cast<std::uint8_t>(tcon | 0x08u); // IE1
        else
            tcon = static_cast<std::uint8_t>(tcon & ~0x08u);
        internal_[0x88] = tcon;
    }

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
    bool aciaTransmitPending_ = false;
    std::uint8_t lcdStatus_ = 0;
    std::uint8_t lcdData_ = 0;
    std::uint8_t lcdInstruction_ = 0;
    std::uint64_t elapsedMachineCycles_ = 0;
    bool romLoaded_ = false;
};

} // namespace technox
