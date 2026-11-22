#pragma once

// Architectural foundation and initial instruction subset for the classic
// Intel MCS-51 / 8051 core. Unsupported instructions are reported explicitly;
// this is not yet a complete 8051 emulator.

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

enum class MCS51StepResult : std::uint8_t { executed, unsupportedOpcode };

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

    MCS51StepResult step(MCS51Bus& bus) noexcept {
        const auto opcode = fetch8(bus);
        std::uint8_t value = 0;
        unsigned cycles = 1;

        if (opcode == 0x00) { // NOP
        } else if (opcode == 0x02) { // LJMP addr16
            const auto high = fetch8(bus);
            const auto low = fetch8(bus);
            registers_.pc = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(high) << 8u) | low);
            cycles = 2;
        } else if (opcode == 0x80) { // SJMP rel
            const auto rel = static_cast<std::int8_t>(fetch8(bus));
            registers_.pc = static_cast<std::uint16_t>(registers_.pc + rel);
            cycles = 2;
        } else if (opcode == 0x74) { // MOV A,#data
            registers_.a = fetch8(bus);
        } else if (opcode >= 0x78 && opcode <= 0x7f) { // MOV Rn,#data
            writeRegister(bus, opcode & 7u, fetch8(bus));
        } else if (opcode == 0x75) { // MOV direct,#data
            const auto address = fetch8(bus);
            bus.writeInternal(address, fetch8(bus));
            cycles = 2;
        } else if (opcode == 0xe5) { // MOV A,direct
            registers_.a = bus.readInternal(fetch8(bus));
        } else if (opcode == 0xf5) { // MOV direct,A
            bus.writeInternal(fetch8(bus), registers_.a);
        } else if (opcode == 0x85) { // MOV direct,direct (source byte first)
            const auto source = fetch8(bus);
            const auto destination = fetch8(bus);
            bus.writeInternal(destination, bus.readInternal(source));
            cycles = 2;
        } else if (opcode >= 0xe8 && opcode <= 0xef) { // MOV A,Rn
            registers_.a = readRegister(bus, opcode & 7u);
        } else if (opcode >= 0xf8 && opcode <= 0xff) { // MOV Rn,A
            writeRegister(bus, opcode & 7u, registers_.a);
        } else if (opcode == 0x04) { // INC A
            ++registers_.a;
        } else if (opcode >= 0x08 && opcode <= 0x0f) { // INC Rn
            writeRegister(bus, opcode & 7u,
                          static_cast<std::uint8_t>(readRegister(bus, opcode & 7u) + 1u));
        } else if (opcode == 0x14) { // DEC A
            --registers_.a;
        } else if (opcode == 0x24) { // ADD A,#data
            add(fetch8(bus), false);
        } else if (opcode == 0x34) { // ADDC A,#data
            add(fetch8(bus), registers_.psw.carry);
        } else if (opcode == 0x25) { // ADD A,direct
            add(bus.readInternal(fetch8(bus)), false);
        } else if (opcode == 0x26 || opcode == 0x27) { // ADD A,@Ri
            const auto address = readRegister(bus, opcode & 1u);
            add(bus.readInternal(address), false);
        } else if (opcode >= 0x28 && opcode <= 0x2f) { // ADD A,Rn
            add(readRegister(bus, opcode & 7u), false);
        } else if (opcode == 0x35) { // ADDC A,direct
            add(bus.readInternal(fetch8(bus)), registers_.psw.carry);
        } else if (opcode == 0x36 || opcode == 0x37) { // ADDC A,@Ri
            const auto address = readRegister(bus, opcode & 1u);
            add(bus.readInternal(address), registers_.psw.carry);
        } else if (opcode >= 0x38 && opcode <= 0x3f) { // ADDC A,Rn
            add(readRegister(bus, opcode & 7u), registers_.psw.carry);
        } else if (opcode == 0x95) { // SUBB A,direct
            subtract(bus.readInternal(fetch8(bus)));
        } else if (opcode == 0x96 || opcode == 0x97) { // SUBB A,@Ri
            const auto address = readRegister(bus, opcode & 1u);
            subtract(bus.readInternal(address));
        } else if (opcode >= 0x98 && opcode <= 0x9f) { // SUBB A,Rn
            subtract(readRegister(bus, opcode & 7u));
        } else if (opcode == 0x94) { // SUBB A,#data
            subtract(fetch8(bus));
        } else if (opcode == 0x05) { // INC direct
            const auto address = fetch8(bus);
            bus.writeInternal(address, static_cast<std::uint8_t>(bus.readInternal(address) + 1u));
        } else if (opcode == 0x06 || opcode == 0x07) { // INC @Ri
            const auto address = readRegister(bus, opcode & 1u);
            bus.writeInternal(address, static_cast<std::uint8_t>(bus.readInternal(address) + 1u));
        } else if (opcode == 0x15) { // DEC direct
            const auto address = fetch8(bus);
            bus.writeInternal(address, static_cast<std::uint8_t>(bus.readInternal(address) - 1u));
        } else if (opcode == 0x16 || opcode == 0x17) { // DEC @Ri
            const auto address = readRegister(bus, opcode & 1u);
            bus.writeInternal(address, static_cast<std::uint8_t>(bus.readInternal(address) - 1u));
        } else if (opcode >= 0x18 && opcode <= 0x1f) { // DEC Rn
            writeRegister(bus, opcode & 7u,
                          static_cast<std::uint8_t>(readRegister(bus, opcode & 7u) - 1u));
        } else if (opcode == 0x60 || opcode == 0x70 ||
                   opcode == 0x40 || opcode == 0x50) { // JZ/JNZ/JC/JNC rel
            const auto rel = static_cast<std::int8_t>(fetch8(bus));
            const bool take = opcode == 0x60 ? registers_.a == 0 :
                              opcode == 0x70 ? registers_.a != 0 :
                              opcode == 0x40 ? registers_.psw.carry :
                                               !registers_.psw.carry;
            if (take) registers_.pc = static_cast<std::uint16_t>(registers_.pc + rel);
            cycles = 2;
        } else if (opcode == 0xe6 || opcode == 0xe7) { // MOV A,@Ri
            registers_.a = bus.readInternal(readRegister(bus, opcode & 1u));
        } else if (opcode == 0xf6 || opcode == 0xf7) { // MOV @Ri,A
            bus.writeInternal(readRegister(bus, opcode & 1u), registers_.a);
        } else if (opcode == 0x76 || opcode == 0x77) { // MOV @Ri,#data
            const auto value = fetch8(bus);
            bus.writeInternal(readRegister(bus, opcode & 1u), value);
        } else if (opcode == 0xa3) { // INC DPTR
            ++registers_.dptr;
            cycles = 2;
        } else if (opcode == 0xe4) { // CLR A
            registers_.a = 0;
        } else if (opcode == 0xf4) { // CPL A
            registers_.a = static_cast<std::uint8_t>(~registers_.a);
        } else {
            registers_.pc = static_cast<std::uint16_t>(registers_.pc - 1u);
            return MCS51StepResult::unsupportedOpcode;
        }

        updateParity();
        totalCycles_ += cycles;
        return MCS51StepResult::executed;
    }
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
    std::uint8_t fetch8(MCS51Bus& bus) noexcept {
        const auto value = bus.readCode(registers_.pc);
        registers_.pc = static_cast<std::uint16_t>(registers_.pc + 1u);
        return value;
    }

    std::uint8_t readRegister(MCS51Bus& bus, std::uint8_t index) const noexcept {
        return bus.readInternal(static_cast<std::uint8_t>(
            registers_.psw.registerBankBase() + (index & 7u)));
    }

    void writeRegister(MCS51Bus& bus, std::uint8_t index, std::uint8_t value) noexcept {
        bus.writeInternal(static_cast<std::uint8_t>(
            registers_.psw.registerBankBase() + (index & 7u)), value);
    }

    void add(std::uint8_t operand, bool carryIn) noexcept {
        const auto a = registers_.a;
        const unsigned carry = carryIn ? 1u : 0u;
        const unsigned sum = static_cast<unsigned>(a) + operand + carry;
        const auto result = static_cast<std::uint8_t>(sum);
        registers_.psw.carry = sum > 0xffu;
        registers_.psw.auxiliaryCarry =
            ((a & 0x0fu) + (operand & 0x0fu) + carry) > 0x0fu;
        registers_.psw.overflow =
            ((~(a ^ operand) & (a ^ result) & 0x80u) != 0);
        registers_.a = result;
    }

    void subtract(std::uint8_t operand) noexcept {
        const auto a = registers_.a;
        const unsigned borrow = registers_.psw.carry ? 1u : 0u;
        const unsigned subtrahend = static_cast<unsigned>(operand) + borrow;
        const auto result = static_cast<std::uint8_t>(
            static_cast<unsigned>(a) - subtrahend);
        registers_.psw.carry = static_cast<unsigned>(a) < subtrahend;
        registers_.psw.auxiliaryCarry =
            static_cast<unsigned>(a & 0x0fu) <
            static_cast<unsigned>(operand & 0x0fu) + borrow;
        registers_.psw.overflow =
            (((a ^ operand) & (a ^ result) & 0x80u) != 0);
        registers_.a = result;
    }

    void updateParity() noexcept {
        std::uint8_t bits = registers_.a;
        bits ^= static_cast<std::uint8_t>(bits >> 4u);
        bits ^= static_cast<std::uint8_t>(bits >> 2u);
        bits ^= static_cast<std::uint8_t>(bits >> 1u);
        registers_.psw.parity = (bits & 1u) != 0;
    }

    MCS51Registers registers_{};
    bool halted_ = false;
    std::uint64_t totalCycles_ = 0;
};

} // namespace technox
