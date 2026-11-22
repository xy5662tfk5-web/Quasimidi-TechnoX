#include "../Source/MCS51Core.h"

#include <cassert>
#include <cstdint>

namespace {
class TestBus final : public technox::MCS51Bus {
public:
    std::uint8_t code[65536]{};
    std::uint8_t internal[256]{};
    std::uint8_t external[65536]{};
    std::uint8_t readCode(std::uint16_t a) const noexcept override { return code[a]; }
    std::uint8_t readInternal(std::uint8_t a) const noexcept override { return internal[a]; }
    void writeInternal(std::uint8_t a, std::uint8_t v) noexcept override { internal[a] = v; }
    std::uint8_t readExternal(std::uint16_t a) noexcept override { return external[a]; }
    void writeExternal(std::uint16_t a, std::uint8_t v) noexcept override { external[a] = v; }
};
}

int main() {
    using namespace technox;

    MCS51Core cpu;
    cpu.reset();
    assert(cpu.registers().pc == 0);
    assert(cpu.registers().sp == 0x07);
    assert(cpu.registers().dptr == 0);
    assert(cpu.totalCycles() == 0);
    assert(!cpu.halted());

    auto& regs = cpu.registersForTesting();
    regs.psw.unpack(0x98);
    assert(regs.psw.pack() == 0x98);
    assert(regs.psw.registerBankBase() == 0x18);
    regs.psw.unpack(0x08);
    assert(regs.psw.registerBankBase() == 0x08);
    regs.psw.unpack(0x00);
    assert(regs.psw.registerBankBase() == 0x00);

    regs.setDph(0x12);
    regs.setDpl(0x34);
    assert(regs.dptr == 0x1234);
    regs.setDpl(0xab);
    assert(regs.dptr == 0x12ab);

    assert(MCS51Core::bitAddressToByte(0x00) == 0x20);
    assert(MCS51Core::bitAddressToBit(0x00) == 0);
    assert(MCS51Core::bitAddressToByte(0x7f) == 0x2f);
    assert(MCS51Core::bitAddressToBit(0x7f) == 7);
    assert(MCS51Core::bitAddressToByte(0x80) == 0x80);
    assert(MCS51Core::bitAddressToBit(0x87) == 7);
    assert(MCS51Core::isBitAddressable(0x20));
    assert(MCS51Core::isBitAddressable(0x2f));
    assert(MCS51Core::isBitAddressable(0x80));
    assert(MCS51Core::isBitAddressable(0xf8));
    assert(!MCS51Core::isBitAddressable(0x30));
    assert(!MCS51Core::isBitAddressable(0x81));

    TestBus bus;
    MCS51Core exec;
    bus.code[0] = 0x74; bus.code[1] = 0x7f; // MOV A,#7F
    bus.code[2] = 0x24; bus.code[3] = 0x01; // ADD A,#01
    bus.code[4] = 0x78; bus.code[5] = 0x42; // MOV R0,#42
    bus.code[6] = 0xe8;                     // MOV A,R0
    bus.code[7] = 0x80; bus.code[8] = 0xfe; // SJMP to itself

    assert(exec.step(bus) == MCS51StepResult::executed);
    assert(exec.registers().a == 0x7f);
    assert(exec.step(bus) == MCS51StepResult::executed);
    assert(exec.registers().a == 0x80);
    assert(!exec.registers().psw.carry);
    assert(exec.registers().psw.overflow);
    assert(exec.step(bus) == MCS51StepResult::executed);
    assert(bus.internal[0] == 0x42);
    assert(exec.step(bus) == MCS51StepResult::executed);
    assert(exec.registers().a == 0x42);
    assert(exec.step(bus) == MCS51StepResult::executed);
    assert(exec.registers().pc == 7);
    assert(exec.totalCycles() == 6);

    TestBus unsupportedBus;
    unsupportedBus.code[0] = 0xa5; // reserved opcode in classic 8051
    MCS51Core unsupported;
    assert(unsupported.step(unsupportedBus) == MCS51StepResult::unsupportedOpcode);
    assert(unsupported.registers().pc == 0);

    regs.pc = 0xeb32;
    regs.a = 0x55;
    cpu.reset();
    assert(cpu.registers().pc == 0);
    assert(cpu.registers().a == 0);
    return 0;
}
