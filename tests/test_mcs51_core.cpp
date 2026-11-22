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

    // Indirect addressing, register arithmetic and conditional branch coverage.
    TestBus addressingBus;
    addressingBus.code[0] = 0x79; addressingBus.code[1] = 0x40; // MOV R1,#40
    addressingBus.code[2] = 0x77; addressingBus.code[3] = 0x10; // MOV @R1,#10
    addressingBus.code[4] = 0xe7;                               // MOV A,@R1
    addressingBus.code[5] = 0x29;                               // ADD A,R1
    addressingBus.code[6] = 0x94; addressingBus.code[7] = 0x10; // SUBB A,#10
    addressingBus.code[8] = 0x70; addressingBus.code[9] = 0x02; // JNZ +2
    addressingBus.code[10] = 0x74; addressingBus.code[11] = 0xff;
    addressingBus.code[12] = 0x74; addressingBus.code[13] = 0x55;
    MCS51Core addressingCpu;
    for (int i = 0; i < 5; ++i)
        assert(addressingCpu.step(addressingBus) == MCS51StepResult::executed);
    assert(addressingBus.internal[0x40] == 0x10);
    assert(addressingCpu.registers().a == 0x40);
    assert(addressingCpu.step(addressingBus) == MCS51StepResult::executed);
    assert(addressingCpu.registers().pc == 12);
    assert(addressingCpu.step(addressingBus) == MCS51StepResult::executed);
    assert(addressingCpu.registers().a == 0x55);

    // Carry, auxiliary carry and overflow are independently derived by ADD.
    TestBus flagsBus;
    flagsBus.code[0] = 0x74; flagsBus.code[1] = 0xff; // MOV A,#FF
    flagsBus.code[2] = 0x24; flagsBus.code[3] = 0x01; // ADD A,#01
    MCS51Core flagsCpu;
    assert(flagsCpu.step(flagsBus) == MCS51StepResult::executed);
    assert(flagsCpu.step(flagsBus) == MCS51StepResult::executed);
    assert(flagsCpu.registers().a == 0x00);
    assert(flagsCpu.registers().psw.carry);
    assert(flagsCpu.registers().psw.auxiliaryCarry);
    assert(!flagsCpu.registers().psw.overflow);

    // Logical instruction families and accumulator rotates.
    TestBus logicBus;
    logicBus.code[0] = 0x74; logicBus.code[1] = 0xf0; // MOV A,#F0
    logicBus.code[2] = 0x54; logicBus.code[3] = 0x3c; // ANL A,#3C -> 30
    logicBus.code[4] = 0x44; logicBus.code[5] = 0x05; // ORL A,#05 -> 35
    logicBus.code[6] = 0x64; logicBus.code[7] = 0x0f; // XRL A,#0F -> 3A
    logicBus.code[8] = 0x23;                       // RL A -> 74
    logicBus.code[9] = 0x03;                       // RR A -> 3A
    logicBus.code[10] = 0xc4;                      // SWAP A -> A3
    MCS51Core logicCpu;
    for (int i = 0; i < 7; ++i)
        assert(logicCpu.step(logicBus) == MCS51StepResult::executed);
    assert(logicCpu.registers().a == 0xa3);

    TestBus directLogicBus;
    directLogicBus.internal[0x30] = 0x55;
    directLogicBus.code[0] = 0x75; directLogicBus.code[1] = 0x30; directLogicBus.code[2] = 0x0f;
    directLogicBus.code[3] = 0x43; directLogicBus.code[4] = 0x30; directLogicBus.code[5] = 0x80;
    directLogicBus.code[6] = 0x53; directLogicBus.code[7] = 0x30; directLogicBus.code[8] = 0xf0;
    MCS51Core directLogicCpu;
    assert(directLogicCpu.step(directLogicBus) == MCS51StepResult::executed);
    assert(directLogicCpu.step(directLogicBus) == MCS51StepResult::executed);
    assert(directLogicCpu.step(directLogicBus) == MCS51StepResult::executed);
    assert(directLogicBus.internal[0x30] == 0x80);

    TestBus rotateCarryBus;
    rotateCarryBus.code[0] = 0x74; rotateCarryBus.code[1] = 0x80;
    rotateCarryBus.code[2] = 0x33; // RLC A: 80 -> 00, carry=1
    rotateCarryBus.code[3] = 0x13; // RRC A: 00 with carry -> 80, carry=0
    MCS51Core rotateCarryCpu;
    assert(rotateCarryCpu.step(rotateCarryBus) == MCS51StepResult::executed);
    assert(rotateCarryCpu.step(rotateCarryBus) == MCS51StepResult::executed);
    assert(rotateCarryCpu.registers().a == 0x00);
    assert(rotateCarryCpu.registers().psw.carry);
    assert(rotateCarryCpu.step(rotateCarryBus) == MCS51StepResult::executed);
    assert(rotateCarryCpu.registers().a == 0x80);
    assert(!rotateCarryCpu.registers().psw.carry);

    // LCALL/RET preserve the full 16-bit return address on the internal stack.
    TestBus callBus;
    callBus.code[0] = 0x12; callBus.code[1] = 0x00; callBus.code[2] = 0x10; // LCALL 0010
    callBus.code[0x10] = 0x74; callBus.code[0x11] = 0x66; // MOV A,#66
    callBus.code[0x12] = 0x22; // RET
    MCS51Core callCpu;
    assert(callCpu.step(callBus) == MCS51StepResult::executed);
    assert(callCpu.registers().pc == 0x0010);
    assert(callCpu.registers().sp == 0x09);
    assert(callBus.internal[0x08] == 0x03); // return low
    assert(callBus.internal[0x09] == 0x00); // return high
    assert(callCpu.step(callBus) == MCS51StepResult::executed);
    assert(callCpu.registers().a == 0x66);
    assert(callCpu.step(callBus) == MCS51StepResult::executed);
    assert(callCpu.registers().pc == 0x0003);
    assert(callCpu.registers().sp == 0x07);

    // PUSH/POP use the hardware stack growth convention and synchronize SP SFR.
    TestBus pushPopBus;
    pushPopBus.code[0] = 0x75; pushPopBus.code[1] = 0x30; pushPopBus.code[2] = 0xa5;
    pushPopBus.code[3] = 0xc0; pushPopBus.code[4] = 0x30; // PUSH 30
    pushPopBus.code[5] = 0x75; pushPopBus.code[6] = 0x30; pushPopBus.code[7] = 0x00;
    pushPopBus.code[8] = 0xd0; pushPopBus.code[9] = 0x30; // POP 30
    MCS51Core pushPopCpu;
    for (int i = 0; i < 4; ++i)
        assert(pushPopCpu.step(pushPopBus) == MCS51StepResult::executed);
    assert(pushPopBus.internal[0x30] == 0xa5);
    assert(pushPopCpu.registers().sp == 0x07);
    assert(pushPopBus.internal[0x81] == 0x07);

    // Bit-addressable IRAM/SFR operations, carry transfer and bit branches.
    TestBus bitBus;
    bitBus.code[0] = 0xd2; bitBus.code[1] = 0x02; // SETB bit 2 -> IRAM 20.2
    bitBus.code[2] = 0xa2; bitBus.code[3] = 0x02; // MOV C,bit 2
    bitBus.code[4] = 0xc2; bitBus.code[5] = 0x02; // CLR bit 2
    bitBus.code[6] = 0x20; bitBus.code[7] = 0x02; bitBus.code[8] = 0x02; // JB bit2,+2 not taken
    bitBus.code[9] = 0x92; bitBus.code[10] = 0x03; // MOV bit3,C
    bitBus.code[11] = 0x10; bitBus.code[12] = 0x03; bitBus.code[13] = 0x02; // JBC bit3,+2
    bitBus.code[14] = 0x74; bitBus.code[15] = 0xff;
    bitBus.code[16] = 0x74; bitBus.code[17] = 0x5a;
    MCS51Core bitCpu;
    for (int i = 0; i < 5; ++i)
        assert(bitCpu.step(bitBus) == MCS51StepResult::executed);
    assert(bitCpu.registers().psw.carry);
    assert((bitBus.internal[0x20] & 0x04u) == 0);
    assert((bitBus.internal[0x20] & 0x08u) != 0);
    assert(bitCpu.step(bitBus) == MCS51StepResult::executed);
    assert(bitCpu.registers().pc == 16);
    assert((bitBus.internal[0x20] & 0x08u) == 0);
    assert(bitCpu.step(bitBus) == MCS51StepResult::executed);
    assert(bitCpu.registers().a == 0x5a);

    // Direct SFR reads/writes are reflected in the CPU's architectural registers.
    TestBus sfrBus;
    sfrBus.code[0] = 0x75; sfrBus.code[1] = 0xe0; sfrBus.code[2] = 0x7b; // MOV ACC,#7B
    sfrBus.code[3] = 0x75; sfrBus.code[4] = 0x82; sfrBus.code[5] = 0x34; // MOV DPL,#34
    sfrBus.code[6] = 0x75; sfrBus.code[7] = 0x83; sfrBus.code[8] = 0x12; // MOV DPH,#12
    sfrBus.code[9] = 0x75; sfrBus.code[10] = 0xd0; sfrBus.code[11] = 0x08; // PSW bank 1
    sfrBus.code[12] = 0x79; sfrBus.code[13] = 0xa6; // MOV R1,#A6 (bank 1)
    sfrBus.code[14] = 0xe5; sfrBus.code[15] = 0xe0; // MOV A,ACC
    MCS51Core sfrCpu;
    for (int i = 0; i < 6; ++i)
        assert(sfrCpu.step(sfrBus) == MCS51StepResult::executed);
    assert(sfrCpu.registers().a == 0x7b);
    assert(sfrCpu.registers().dptr == 0x1234);
    assert(sfrBus.internal[0x09] == 0xa6);
    assert(sfrCpu.step(sfrBus) == MCS51StepResult::executed);
    assert(sfrCpu.registers().a == 0x7b);

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
