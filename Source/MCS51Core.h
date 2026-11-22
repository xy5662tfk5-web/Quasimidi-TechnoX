#pragma once

// Classic MCS-51 instruction core. Peripheral timing and pin-level behavior
// are supplied by MCS51Bus; unsupported/reserved opcodes are reported explicitly.

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

    // Indirect @Ri and stack accesses use upper IRAM on 8032 (80h-FFh),
    // while direct accesses to the same addresses select SFRs.
    virtual std::uint8_t readIndirect(std::uint8_t address) const noexcept {
        return readInternal(address);
    }
    virtual void writeIndirect(std::uint8_t address, std::uint8_t value) noexcept {
        writeInternal(address, value);
    }

    // MOVX @Ri drives the P2 output latch as the high address byte. This can
    // differ from reading the external pin state through MOV A,P2.
    virtual std::uint8_t readPortLatch(std::uint8_t portAddress) const noexcept {
        return readInternal(portAddress);
    }

    // External data space is separately mapped; IC4 XDATA hardware semantics
    // must be supplied by a device model, not guessed by the CPU core.
    virtual std::uint8_t readExternal(std::uint16_t address) noexcept = 0;
    virtual void writeExternal(std::uint16_t address, std::uint8_t value) noexcept = 0;

    // Advance timer/UART/peripheral models by classic 8051 machine cycles.
    virtual void advanceMachineCycles(unsigned /*cycles*/) noexcept {}
};

enum class MCS51StepResult : std::uint8_t { executed, unsupportedOpcode };

class MCS51Core {
public:
    static constexpr std::uint32_t kAddressSpaceSize = 0x10000u;

    void reset() noexcept {
        registers_ = MCS51Registers{};
        halted_ = false;
        totalCycles_ = 0;
        interruptDepth_ = 0;
        interruptPriorityStack_ = {};
    }

    const MCS51Registers& registers() const noexcept { return registers_; }
    MCS51Registers& registersForTesting() noexcept { return registers_; }

    MCS51StepResult step(MCS51Bus& bus) noexcept {
        if (servicePendingInterrupt(bus))
            return MCS51StepResult::executed;
        const auto opcode = fetch8(bus);
        unsigned cycles = 1;

        if (opcode == 0x00) { // NOP
        } else if (opcode == 0x90) { // MOV DPTR,#data16
            const auto high = fetch8(bus);
            const auto low = fetch8(bus);
            registers_.dptr = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(high) << 8u) | low);
            cycles = 2;
        } else if (opcode == 0x02) { // LJMP addr16
            const auto high = fetch8(bus);
            const auto low = fetch8(bus);
            registers_.pc = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(high) << 8u) | low);
            cycles = 2;
        } else if ((opcode & 0x1fu) == 0x01u) { // AJMP addr11
            const auto low = fetch8(bus);
            const auto target = static_cast<std::uint16_t>(
                (registers_.pc & 0xf800u) |
                (static_cast<std::uint16_t>(opcode & 0xe0u) << 3u) | low);
            registers_.pc = target;
            cycles = 2;
        } else if ((opcode & 0x1fu) == 0x11u) { // ACALL addr11
            const auto low = fetch8(bus);
            const auto returnAddress = registers_.pc;
            push(bus, static_cast<std::uint8_t>(returnAddress & 0xffu));
            push(bus, static_cast<std::uint8_t>(returnAddress >> 8u));
            registers_.pc = static_cast<std::uint16_t>(
                (returnAddress & 0xf800u) |
                (static_cast<std::uint16_t>(opcode & 0xe0u) << 3u) | low);
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
            writeDirect(bus, address, fetch8(bus));
            cycles = 2;
        } else if (opcode == 0xe5) { // MOV A,direct
            registers_.a = readDirect(bus, fetch8(bus));
        } else if (opcode == 0xf5) { // MOV direct,A
            writeDirect(bus, fetch8(bus), registers_.a);
        } else if (opcode == 0x85) { // MOV direct,direct (source byte first)
            const auto source = fetch8(bus);
            const auto destination = fetch8(bus);
            writeDirect(bus, destination, readDirect(bus, source));
            cycles = 2;
        } else if (opcode >= 0x88 && opcode <= 0x8f) { // MOV direct,Rn
            const auto destination = fetch8(bus);
            writeDirect(bus, destination, readRegister(bus, opcode & 7u));
            cycles = 2;
        } else if (opcode == 0x86 || opcode == 0x87) { // MOV direct,@Ri
            const auto destination = fetch8(bus);
            const auto address = readRegister(bus, opcode & 1u);
            writeDirect(bus, destination, bus.readIndirect(address));
            cycles = 2;
        } else if (opcode >= 0xa8 && opcode <= 0xaf) { // MOV Rn,direct
            writeRegister(bus, opcode & 7u, readDirect(bus, fetch8(bus)));
            cycles = 2;
        } else if (opcode == 0xa6 || opcode == 0xa7) { // MOV @Ri,direct
            const auto source = readDirect(bus, fetch8(bus));
            bus.writeIndirect(readRegister(bus, opcode & 1u), source);
            cycles = 2;
        } else if (opcode >= 0xe8 && opcode <= 0xef) { // MOV A,Rn
            registers_.a = readRegister(bus, opcode & 7u);
        } else if (opcode >= 0xf8) { // MOV Rn,A
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
            add(readDirect(bus, fetch8(bus)), false);
        } else if (opcode == 0x26 || opcode == 0x27) { // ADD A,@Ri
            const auto address = readRegister(bus, opcode & 1u);
            add(bus.readIndirect(address), false);
        } else if (opcode >= 0x28 && opcode <= 0x2f) { // ADD A,Rn
            add(readRegister(bus, opcode & 7u), false);
        } else if (opcode == 0x35) { // ADDC A,direct
            add(readDirect(bus, fetch8(bus)), registers_.psw.carry);
        } else if (opcode == 0x36 || opcode == 0x37) { // ADDC A,@Ri
            const auto address = readRegister(bus, opcode & 1u);
            add(bus.readIndirect(address), registers_.psw.carry);
        } else if (opcode >= 0x38 && opcode <= 0x3f) { // ADDC A,Rn
            add(readRegister(bus, opcode & 7u), registers_.psw.carry);
        } else if (opcode == 0x95) { // SUBB A,direct
            subtract(readDirect(bus, fetch8(bus)));
        } else if (opcode == 0x96 || opcode == 0x97) { // SUBB A,@Ri
            const auto address = readRegister(bus, opcode & 1u);
            subtract(bus.readIndirect(address));
        } else if (opcode >= 0x98 && opcode <= 0x9f) { // SUBB A,Rn
            subtract(readRegister(bus, opcode & 7u));
        } else if (opcode == 0x94) { // SUBB A,#data
            subtract(fetch8(bus));
        } else if (opcode == 0x05) { // INC direct
            const auto address = fetch8(bus);
            writeDirect(bus, address, static_cast<std::uint8_t>(readDirect(bus, address) + 1u));
        } else if (opcode == 0x06 || opcode == 0x07) { // INC @Ri
            const auto address = readRegister(bus, opcode & 1u);
            bus.writeIndirect(address, static_cast<std::uint8_t>(bus.readIndirect(address) + 1u));
        } else if (opcode == 0x15) { // DEC direct
            const auto address = fetch8(bus);
            writeDirect(bus, address, static_cast<std::uint8_t>(readDirect(bus, address) - 1u));
        } else if (opcode == 0x16 || opcode == 0x17) { // DEC @Ri
            const auto address = readRegister(bus, opcode & 1u);
            bus.writeIndirect(address, static_cast<std::uint8_t>(bus.readIndirect(address) - 1u));
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
            registers_.a = bus.readIndirect(readRegister(bus, opcode & 1u));
        } else if (opcode == 0xf6 || opcode == 0xf7) { // MOV @Ri,A
            bus.writeIndirect(readRegister(bus, opcode & 1u), registers_.a);
        } else if (opcode == 0x76 || opcode == 0x77) { // MOV @Ri,#data
            const auto value = fetch8(bus);
            bus.writeIndirect(readRegister(bus, opcode & 1u), value);
        } else if (opcode == 0x42) { // ORL direct,A
            const auto address = fetch8(bus);
            writeDirect(bus, address, static_cast<std::uint8_t>(readDirect(bus, address) | registers_.a));
            cycles = 2;
        } else if (opcode == 0x43) { // ORL direct,#data
            const auto address = fetch8(bus);
            const auto immediate = fetch8(bus);
            writeDirect(bus, address, static_cast<std::uint8_t>(readDirect(bus, address) | immediate));
            cycles = 2;
        } else if (opcode == 0x44) { // ORL A,#data
            registers_.a |= fetch8(bus);
        } else if (opcode == 0x45) { // ORL A,direct
            registers_.a |= readDirect(bus, fetch8(bus));
        } else if (opcode == 0x46 || opcode == 0x47) { // ORL A,@Ri
            registers_.a |= bus.readIndirect(readRegister(bus, opcode & 1u));
        } else if (opcode >= 0x48 && opcode <= 0x4f) { // ORL A,Rn
            registers_.a |= readRegister(bus, opcode & 7u);
        } else if (opcode == 0x52) { // ANL direct,A
            const auto address = fetch8(bus);
            writeDirect(bus, address, static_cast<std::uint8_t>(readDirect(bus, address) & registers_.a));
            cycles = 2;
        } else if (opcode == 0x53) { // ANL direct,#data
            const auto address = fetch8(bus);
            const auto immediate = fetch8(bus);
            writeDirect(bus, address, static_cast<std::uint8_t>(readDirect(bus, address) & immediate));
            cycles = 2;
        } else if (opcode == 0x54) { // ANL A,#data
            registers_.a &= fetch8(bus);
        } else if (opcode == 0x55) { // ANL A,direct
            registers_.a &= readDirect(bus, fetch8(bus));
        } else if (opcode == 0x56 || opcode == 0x57) { // ANL A,@Ri
            registers_.a &= bus.readIndirect(readRegister(bus, opcode & 1u));
        } else if (opcode >= 0x58 && opcode <= 0x5f) { // ANL A,Rn
            registers_.a &= readRegister(bus, opcode & 7u);
        } else if (opcode == 0x62) { // XRL direct,A
            const auto address = fetch8(bus);
            writeDirect(bus, address, static_cast<std::uint8_t>(readDirect(bus, address) ^ registers_.a));
            cycles = 2;
        } else if (opcode == 0x63) { // XRL direct,#data
            const auto address = fetch8(bus);
            const auto immediate = fetch8(bus);
            writeDirect(bus, address, static_cast<std::uint8_t>(readDirect(bus, address) ^ immediate));
            cycles = 2;
        } else if (opcode == 0x64) { // XRL A,#data
            registers_.a ^= fetch8(bus);
        } else if (opcode == 0x65) { // XRL A,direct
            registers_.a ^= readDirect(bus, fetch8(bus));
        } else if (opcode == 0x66 || opcode == 0x67) { // XRL A,@Ri
            registers_.a ^= bus.readIndirect(readRegister(bus, opcode & 1u));
        } else if (opcode >= 0x68 && opcode <= 0x6f) { // XRL A,Rn
            registers_.a ^= readRegister(bus, opcode & 7u);
        } else if (opcode == 0x10 || opcode == 0x20 || opcode == 0x30) { // JBC/JB/JNB bit,rel
            const auto bitAddress = fetch8(bus);
            const auto rel = static_cast<std::int8_t>(fetch8(bus));
            const bool bit = readBit(bus, bitAddress);
            const bool take = opcode == 0x10 ? bit : opcode == 0x20 ? bit : !bit;
            if (take) {
                if (opcode == 0x10) writeBit(bus, bitAddress, false);
                registers_.pc = static_cast<std::uint16_t>(registers_.pc + rel);
            }
            cycles = 2;
        } else if (opcode == 0xc3) { // CLR C
            registers_.psw.carry = false;
        } else if (opcode == 0xd3) { // SETB C
            registers_.psw.carry = true;
        } else if (opcode == 0xb3) { // CPL C
            registers_.psw.carry = !registers_.psw.carry;
        } else if (opcode == 0x72 || opcode == 0x82 ||
                   opcode == 0xa0 || opcode == 0xb0) { // ORL/ANL C,bit or /bit
            const bool bit = readBit(bus, fetch8(bus));
            const bool operand = (opcode == 0xa0 || opcode == 0xb0) ? !bit : bit;
            if (opcode == 0x72 || opcode == 0xa0)
                registers_.psw.carry = registers_.psw.carry || operand;
            else
                registers_.psw.carry = registers_.psw.carry && operand;
            cycles = 2;
        } else if (opcode == 0x92) { // MOV bit,C
            writeBit(bus, fetch8(bus), registers_.psw.carry);
            cycles = 2;
        } else if (opcode == 0xa2) { // MOV C,bit
            registers_.psw.carry = readBit(bus, fetch8(bus));
            cycles = 2;
        } else if (opcode == 0xb2 || opcode == 0xc2 || opcode == 0xd2) { // CPL/CLR/SETB bit
            const auto bitAddress = fetch8(bus);
            const bool value = opcode == 0xb2 ? !readBit(bus, bitAddress) : opcode == 0xd2;
            writeBit(bus, bitAddress, value);
            cycles = 2;
        } else if (opcode == 0x12) { // LCALL addr16
            const auto high = fetch8(bus);
            const auto low = fetch8(bus);
            push(bus, static_cast<std::uint8_t>(registers_.pc & 0xffu));
            push(bus, static_cast<std::uint8_t>(registers_.pc >> 8u));
            registers_.pc = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(high) << 8u) | low);
            cycles = 2;
        } else if (opcode == 0x22 || opcode == 0x32) { // RET / RETI
            const auto high = pop(bus);
            const auto low = pop(bus);
            registers_.pc = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(high) << 8u) | low);
            if (opcode == 0x32 && interruptDepth_ != 0)
                --interruptDepth_;
            cycles = 2;
        } else if (opcode == 0xc0) { // PUSH direct
            push(bus, readDirect(bus, fetch8(bus)));
            cycles = 2;
        } else if (opcode == 0xd0) { // POP direct
            const auto address = fetch8(bus);
            writeDirect(bus, address, pop(bus));
            cycles = 2;
        } else if (opcode >= 0xb8 && opcode <= 0xbf) { // CJNE Rn,#data,rel
            const auto immediate = fetch8(bus);
            const auto rel = static_cast<std::int8_t>(fetch8(bus));
            const auto operand = readRegister(bus, opcode & 7u);
            registers_.psw.carry = operand < immediate;
            if (operand != immediate)
                registers_.pc = static_cast<std::uint16_t>(registers_.pc + rel);
            cycles = 2;
        } else if (opcode == 0xb4 || opcode == 0xb5) { // CJNE A,#data/direct,rel
            const auto operand = opcode == 0xb4 ? fetch8(bus) : readDirect(bus, fetch8(bus));
            const auto rel = static_cast<std::int8_t>(fetch8(bus));
            registers_.psw.carry = registers_.a < operand;
            if (registers_.a != operand)
                registers_.pc = static_cast<std::uint16_t>(registers_.pc + rel);
            cycles = 2;
        } else if (opcode == 0xb6 || opcode == 0xb7) { // CJNE @Ri,#data,rel
            const auto address = readRegister(bus, opcode & 1u);
            const auto immediate = fetch8(bus);
            const auto rel = static_cast<std::int8_t>(fetch8(bus));
            const auto operand = bus.readIndirect(address);
            registers_.psw.carry = operand < immediate;
            if (operand != immediate)
                registers_.pc = static_cast<std::uint16_t>(registers_.pc + rel);
            cycles = 2;
        } else if (opcode == 0xd5) { // DJNZ direct,rel
            const auto address = fetch8(bus);
            const auto rel = static_cast<std::int8_t>(fetch8(bus));
            const auto value = static_cast<std::uint8_t>(readDirect(bus, address) - 1u);
            writeDirect(bus, address, value);
            if (value != 0)
                registers_.pc = static_cast<std::uint16_t>(registers_.pc + rel);
            cycles = 2;
        } else if (opcode >= 0xd8 && opcode <= 0xdf) { // DJNZ Rn,rel
            const auto rel = static_cast<std::int8_t>(fetch8(bus));
            const auto value = static_cast<std::uint8_t>(readRegister(bus, opcode & 7u) - 1u);
            writeRegister(bus, opcode & 7u, value);
            if (value != 0)
                registers_.pc = static_cast<std::uint16_t>(registers_.pc + rel);
            cycles = 2;
        } else if (opcode == 0xc5) { // XCH A,direct
            const auto address = fetch8(bus);
            const auto value = readDirect(bus, address);
            writeDirect(bus, address, registers_.a);
            registers_.a = value;
        } else if (opcode == 0xc6 || opcode == 0xc7) { // XCH A,@Ri
            const auto address = readRegister(bus, opcode & 1u);
            const auto value = bus.readIndirect(address);
            bus.writeIndirect(address, registers_.a);
            registers_.a = value;
        } else if (opcode >= 0xc8 && opcode <= 0xcf) { // XCH A,Rn
            const auto index = opcode & 7u;
            const auto value = readRegister(bus, index);
            writeRegister(bus, index, registers_.a);
            registers_.a = value;
        } else if (opcode == 0xd6 || opcode == 0xd7) { // XCHD A,@Ri
            const auto address = readRegister(bus, opcode & 1u);
            const auto value = bus.readIndirect(address);
            bus.writeIndirect(address, static_cast<std::uint8_t>((value & 0xf0u) | (registers_.a & 0x0fu)));
            registers_.a = static_cast<std::uint8_t>((registers_.a & 0xf0u) | (value & 0x0fu));
        } else if (opcode == 0xd4) { // DA A
            const auto original = registers_.a;
            std::uint8_t correction = 0;
            bool carry = registers_.psw.carry;
            if ((original & 0x0fu) > 9u || registers_.psw.auxiliaryCarry)
                correction |= 0x06u;
            if (original > 0x99u || carry) {
                correction |= 0x60u;
                carry = true;
            }
            const auto adjusted = static_cast<unsigned>(original) + correction;
            registers_.a = static_cast<std::uint8_t>(adjusted);
            registers_.psw.carry = carry || adjusted > 0xffu;
        } else if (opcode == 0x73) { // JMP @A+DPTR
            registers_.pc = static_cast<std::uint16_t>(registers_.dptr + registers_.a);
            cycles = 2;
        } else if (opcode == 0x83) { // MOVC A,@A+PC
            const auto address = static_cast<std::uint16_t>(registers_.pc + registers_.a);
            registers_.a = bus.readCode(address);
            cycles = 2;
        } else if (opcode == 0x93) { // MOVC A,@A+DPTR
            const auto address = static_cast<std::uint16_t>(registers_.dptr + registers_.a);
            registers_.a = bus.readCode(address);
            cycles = 2;
        } else if (opcode == 0xe0) { // MOVX A,@DPTR
            registers_.a = bus.readExternal(registers_.dptr);
            cycles = 2;
        } else if (opcode == 0xf0) { // MOVX @DPTR,A
            bus.writeExternal(registers_.dptr, registers_.a);
            cycles = 2;
        } else if (opcode == 0xe2 || opcode == 0xe3) { // MOVX A,@Ri
            const auto low = readRegister(bus, opcode & 1u);
            const auto high = bus.readPortLatch(0xa0u); // P2 output latch
            const auto address = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(high) << 8u) | low);
            registers_.a = bus.readExternal(address);
            cycles = 2;
        } else if (opcode == 0xf2 || opcode == 0xf3) { // MOVX @Ri,A
            const auto low = readRegister(bus, opcode & 1u);
            const auto high = bus.readPortLatch(0xa0u); // P2 output latch
            const auto address = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(high) << 8u) | low);
            bus.writeExternal(address, registers_.a);
            cycles = 2;
        } else if (opcode == 0x84) { // DIV AB
            if (registers_.b == 0) {
                registers_.psw.overflow = true;
                registers_.psw.carry = false;
            } else {
                const auto dividend = registers_.a;
                registers_.a = static_cast<std::uint8_t>(dividend / registers_.b);
                registers_.b = static_cast<std::uint8_t>(dividend % registers_.b);
                registers_.psw.overflow = false;
                registers_.psw.carry = false;
            }
            cycles = 4;
        } else if (opcode == 0xa4) { // MUL AB
            const auto product = static_cast<std::uint16_t>(registers_.a) * registers_.b;
            registers_.a = static_cast<std::uint8_t>(product & 0xffu);
            registers_.b = static_cast<std::uint8_t>(product >> 8u);
            registers_.psw.carry = false;
            registers_.psw.overflow = registers_.b != 0;
            cycles = 4;
        } else if (opcode == 0x03) { // RR A
            registers_.a = static_cast<std::uint8_t>((registers_.a >> 1u) | (registers_.a << 7u));
        } else if (opcode == 0x13) { // RRC A
            const bool oldCarry = registers_.psw.carry;
            registers_.psw.carry = (registers_.a & 1u) != 0;
            registers_.a = static_cast<std::uint8_t>((registers_.a >> 1u) | (oldCarry ? 0x80u : 0u));
        } else if (opcode == 0x23) { // RL A
            registers_.a = static_cast<std::uint8_t>((registers_.a << 1u) | (registers_.a >> 7u));
        } else if (opcode == 0x33) { // RLC A
            const bool oldCarry = registers_.psw.carry;
            registers_.psw.carry = (registers_.a & 0x80u) != 0;
            registers_.a = static_cast<std::uint8_t>((registers_.a << 1u) | (oldCarry ? 1u : 0u));
        } else if (opcode == 0xc4) { // SWAP A
            registers_.a = static_cast<std::uint8_t>((registers_.a << 4u) | (registers_.a >> 4u));
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
        syncArchitecturalSfrs(bus);
        bus.advanceMachineCycles(cycles);
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
    bool servicePendingInterrupt(MCS51Bus& bus) noexcept {
        if (interruptDepth_ >= interruptPriorityStack_.size())
            return false;

        const auto interruptEnable = bus.readInternal(0xa8u); // IE
        if ((interruptEnable & 0x80u) == 0)
            return false;
        const auto interruptPriority = bus.readInternal(0xb8u); // IP
        const auto tcon = bus.readInternal(0x88u);

        struct PendingInterrupt {
            std::uint8_t enableMask;
            std::uint8_t flagAddress;
            std::uint8_t flagMask;
            std::uint8_t priorityMask;
            std::uint8_t vectorLow;
            std::uint8_t clearMode; // 1=always, 2=edge-triggered external flag
        };
        static constexpr PendingInterrupt sources[] = {
            {0x01u, 0x88u, 0x02u, 0x01u, 0x03u, 2u}, // INT0
            {0x02u, 0x88u, 0x20u, 0x02u, 0x0bu, 1u}, // Timer 0
            {0x04u, 0x88u, 0x08u, 0x04u, 0x13u, 2u}, // INT1
            {0x08u, 0x88u, 0x80u, 0x08u, 0x1bu, 1u}, // Timer 1
            {0x10u, 0x98u, 0x03u, 0x10u, 0x23u, 0u}, // Serial RI or TI
            {0x20u, 0xc8u, 0xc0u, 0x20u, 0x2bu, 0u}  // 8052/8032 Timer 2
        };

        const int currentPriority = interruptDepth_ == 0
            ? -1 : static_cast<int>(interruptPriorityStack_[interruptDepth_ - 1]);
        for (int priority = 1; priority >= 0; --priority) {
            if (priority <= currentPriority)
                continue;
            for (const auto& source : sources) {
                if ((interruptEnable & source.enableMask) == 0)
                    continue;
                if (((interruptPriority & source.priorityMask) != 0) != (priority != 0))
                    continue;
                const auto flags = bus.readInternal(source.flagAddress);
                if ((flags & source.flagMask) == 0)
                    continue;

                push(bus, static_cast<std::uint8_t>(registers_.pc & 0xffu));
                push(bus, static_cast<std::uint8_t>(registers_.pc >> 8u));
                registers_.pc = source.vectorLow;

                if (source.clearMode == 1u ||
                    (source.clearMode == 2u &&
                     (tcon & (source.vectorLow == 0x03u ? 0x01u : 0x04u)) != 0)) {
                    const auto current = bus.readInternal(source.flagAddress);
                    bus.writeInternal(source.flagAddress,
                        static_cast<std::uint8_t>(current & static_cast<std::uint8_t>(~source.flagMask)));
                }

                interruptPriorityStack_[interruptDepth_++] = static_cast<std::uint8_t>(priority);
                syncArchitecturalSfrs(bus);
                bus.advanceMachineCycles(2);
                totalCycles_ += 2;
                return true;
            }
        }
        return false;
    }

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

    std::uint8_t readDirect(MCS51Bus& bus, std::uint8_t address) const noexcept {
        switch (address) {
            case 0x81: return registers_.sp;
            case 0x82: return registers_.dpl();
            case 0x83: return registers_.dph();
            case 0xd0: return registers_.psw.pack();
            case 0xe0: return registers_.a;
            case 0xf0: return registers_.b;
            default: return bus.readInternal(address);
        }
    }

    void writeDirect(MCS51Bus& bus, std::uint8_t address, std::uint8_t value) noexcept {
        switch (address) {
            case 0x81: registers_.sp = value; break;
            case 0x82: registers_.setDpl(value); break;
            case 0x83: registers_.setDph(value); break;
            case 0xd0: registers_.psw.unpack(value); break;
            case 0xe0: registers_.a = value; break;
            case 0xf0: registers_.b = value; break;
            default: bus.writeInternal(address, value); break;
        }
        if (address == 0x81 || address == 0x82 || address == 0x83 ||
            address == 0xd0 || address == 0xe0 || address == 0xf0)
            bus.writeInternal(address, value);
    }

    bool readBit(MCS51Bus& bus, std::uint8_t bitAddress) const noexcept {
        const auto address = bitAddressToByte(bitAddress);
        return (readDirect(bus, address) & static_cast<std::uint8_t>(1u << bitAddressToBit(bitAddress))) != 0;
    }

    void writeBit(MCS51Bus& bus, std::uint8_t bitAddress, bool value) noexcept {
        const auto address = bitAddressToByte(bitAddress);
        const auto mask = static_cast<std::uint8_t>(1u << bitAddressToBit(bitAddress));
        const auto current = readDirect(bus, address);
        writeDirect(bus, address, value ? static_cast<std::uint8_t>(current | mask)
                                         : static_cast<std::uint8_t>(current & static_cast<std::uint8_t>(~mask)));
    }

    void push(MCS51Bus& bus, std::uint8_t value) noexcept {
        registers_.sp = static_cast<std::uint8_t>(registers_.sp + 1u);
        bus.writeIndirect(registers_.sp, value);
        bus.writeInternal(0x81u, registers_.sp);
    }

    std::uint8_t pop(MCS51Bus& bus) noexcept {
        const auto value = bus.readIndirect(registers_.sp);
        registers_.sp = static_cast<std::uint8_t>(registers_.sp - 1u);
        bus.writeInternal(0x81u, registers_.sp);
        return value;
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

    void syncArchitecturalSfrs(MCS51Bus& bus) const noexcept {
        bus.writeInternal(0x81u, registers_.sp);
        bus.writeInternal(0x82u, registers_.dpl());
        bus.writeInternal(0x83u, registers_.dph());
        bus.writeInternal(0xd0u, registers_.psw.pack());
        bus.writeInternal(0xe0u, registers_.a);
        bus.writeInternal(0xf0u, registers_.b);
    }

    MCS51Registers registers_{};
    bool halted_ = false;
    std::uint64_t totalCycles_ = 0;
    std::array<std::uint8_t, 2> interruptPriorityStack_{};
    std::uint8_t interruptDepth_ = 0;
};

} // namespace technox
