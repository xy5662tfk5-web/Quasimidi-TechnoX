#pragma once

// Architectural foundation for the classic Intel MCS-51 / 8051 core.
// Instruction execution is deliberately added in subsequent commits; this
// file defines CPU state and the memory/peripheral boundary without claiming
// that the original IC4 firmware can already run.

#include <array>
#include <cstdint>

namespace technox {

struct MCS51Flags {
    bool carry = false;       // PSW.7
    bool auxiliaryCarry = false; // PSW.6
    bool user0 = false;       // PSW.5
    bool registerBank1 = false; // PSW.4
    bool registerBank0 = false; // PSW.3
    bool overflow = false;    // PSW.2
    bool user1 = false;       // PSW.1
    bool parity = false;      // PSW.0

    constexpr std::uint8_t pack() const noexcept {
        return static_cast<std::uint8_t>(
            (carry ? 0x80u : 0u) |
            (auxiliaryCarry ? 0x40u : 0u) |
            (user0 ? 0x20u : 0u) |
            (registerBank1 ? 0x10u : 0u) |
            (registerBank0 ? 0x08u : 0u) |
            (overflow ? 0x04u : 0u) |
            (user1 ? 0x02u : 0u) |
            (parity ? 0x01u : 0u));
    }

    constexpr void unpack(std::uint8_t value) noexcept {
        carry = (value & 0x80u) != 0;
        auxiliaryCarry = (value & 0x40u) != 0;
        user0 = (value & 0x20u) != 0;
        registerBank1 = (value & 0x10u) != 0;
        registerBank0 = (value & 0x08u) != 0;
        overflow = (value & 0x04u) != 0;
        user1 = (value & 0x02u) != 0;
        parity = (value & 0x01u) != 0;
    }

    constexpr std::uint8_t registerBankBase() const noexcept {
        return static_cast<std::uint8_t>(
            ((registerBank1 ? 2u : 0u) | (registerBank0 ? 1u : 0u)) * 8u);
    }
};

struct MCS51Registers {
    std::uint16_t pc = 0;
    std::uint8_t a = 0;
    std::uint8_t b = 0;
    std::uint16_t dptr = 0;
    std::uint8_t sp = 0x07;
    MCS51Flags psw{};

    constexpr std::uint8_t dpl() const noexcept {
        return static_cast<std::uint8_t>(dptr & 0xffu);
    }
    constexpr std::uint8_t dph() const noexcept {
        return static_cast<std::uint8_t>(dptr >> 8u);
    }
    constexpr void setDpl(std::uint8_t value) noexcept {
        dptr = static_cast<std::uint16_t>((dptr & 0xff00u) | value);
    }
    constexpr void setDph(std::uint8_t value) noexcept {
        dptr = static_cast<std::uint16_t>((dptr & 0x00ffu) |
                                          (static_cast<std::uint16_t>(value) << 8u));
    }
};

class MCS51Bus {
public:
    virtual ~MCS51Bus() = default;

    // CODE space is 64 KiB on the classic 8051. Implementations may map the
    // IC4 image or banked firmware here.
    virtual std::uint8_t readCode(std::uint16_t address) const noexcept = 0;

    // Internal data space includes lower/upper IRAM and SFR address decoding.
    virtual std::uint8_t readInternal(std::uint8_t address) const noexcept = 0;
    virtual void writeInternal(std::uint8_t address, std::uint8_t value) noexcept = 0;

    // External data space is separately mapped; IC4 XDATA hardware semantics
    // must be supplied by a device model, not guessed by the CPU core.
    virtual std::uint8_t readExternal(std::uint16_t address) noexcept = 0;
    virtual void writeExternal(std::uint16_t address, std::uint8_t value) noexcept = 0;
};

class MCS51Core {
public:
    static constexpr std::uint16_t kAddressSpaceSize = 0x10000;

    void reset() noexcept {
        registers_ = MCS51Registers{};
        halted_ = false;
        totalCycles_ = 0;
    }

    const MCS51Registers& registers() const noexcept { return registers_; }
    MCS51Registers& registersForTesting() noexcept { return registers_; }
    bool halted() const noexcept { return halted_; }
    std::uint64_t totalCycles() const noexcept { return totalCycles_; }

    static constexpr std::uint8_t bitAddressToByte(std::uint8_t bitAddress) noexcept {
        return bitAddress < 0x80u
            ? static_cast<std::uint8_t>(0x20u + (bitAddress >> 3u))
            : static_cast<std::uint8_t>(bitAddress & 0xf8u);
    }

    static constexpr std::uint8_t bitAddressToBit(std::uint8_t bitAddress) noexcept {
        return static_cast<std::uint8_t>(bitAddress & 0x07u);
    }

    static constexpr bool isBitAddressable(std::uint8_t directAddress) noexcept {
        return (directAddress >= 0x20u && directAddress <= 0x2fu) ||
               (directAddress >= 0x80u && (directAddress & 0x07u) == 0u);
    }

private:
    MCS51Registers registers_{};
    bool halted_ = false;
    std::uint64_t totalCycles_ = 0;
};

} // namespace technox
