#include "../Source/MCS51Core.h"

#include <cassert>
#include <cstdint>

namespace {
class TestBus final : public technox::MCS51Bus {
public:
    std::uint8_t code[65536]{};
    std::uint8_t internal[256]{};
    std::uint8_t upperInternal[128]{};
    std::uint8_t external[65536]{};
    std::uint8_t portLatches[4]{};
    unsigned advancedMachineCycles = 0;
    std::uint8_t readCode(std::uint16_t a) const noexcept override { return code[a]; }
    std::uint8_t readInternal(std::uint8_t a) const noexcept override { return internal[a]; }
    std::uint8_t readIndirect(std::uint8_t a) const noexcept override {
        return a < 0x80 ? internal[a] : upperInternal[a - 0x80];
    }
    void writeIndirect(std::uint8_t a, std::uint8_t v) noexcept override {
        if (a < 0x80) internal[a] = v;
        else upperInternal[a - 0x80] = v;
    }
    void writeInternal(std::uint8_t a, std::uint8_t v) noexcept override {
        internal[a] = v;
        if (a == 0x80) portLatches[0] = v;
        if (a == 0x90) portLatches[1] = v;
        if (a == 0xa0) portLatches[2] = v;
        if (a == 0xb0) portLatches[3] = v;
    }
    std::uint8_t readPortLatch(std::uint8_t a) const noexcept override {
        return a == 0x80 ? portLatches[0] : a == 0x90 ? portLatches[1] :
               a == 0xa0 ? portLatches[2] : a == 0xb0 ? portLatches[3] : readInternal(a);
    }
    std::uint8_t readExternal(std::uint16_t a) noexcept override { return external[a]; }
    void writeExternal(std::uint16_t a, std::uint8_t v) noexcept override { external[a] = v; }
    void advanceMachineCycles(unsigned cycles) noexcept override { advancedMachineCycles += cycles; }
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

    // CODE/XDATA access and arithmetic unit instructions.
    TestBus memoryBus;
    memoryBus.code[0] = 0x90; memoryBus.code[1] = 0x12; memoryBus.code[2] = 0x34; // MOV DPTR,#1234
    memoryBus.code[3] = 0x74; memoryBus.code[4] = 0x02; // MOV A,#02
    memoryBus.code[5] = 0x93; // MOVC A,@A+DPTR -> CODE[1236]
    memoryBus.code[0x1236] = 0x66;
    memoryBus.code[6] = 0xe0; // MOVX A,@DPTR
    memoryBus.code[7] = 0xf0; // MOVX @DPTR,A
    memoryBus.external[0x1234] = 0x5a;
    MCS51Core memoryCpu;
    for (int i = 0; i < 3; ++i)
        assert(memoryCpu.step(memoryBus) == MCS51StepResult::executed);
    assert(memoryCpu.registers().dptr == 0x1234);
    assert(memoryCpu.registers().a == 0x66); // MOVC reads CODE[DPTR+A]
    assert(memoryCpu.step(memoryBus) == MCS51StepResult::executed);
    assert(memoryCpu.registers().a == 0x5a); // MOVX then reads XDATA[DPTR]
    assert(memoryCpu.step(memoryBus) == MCS51StepResult::executed);
    assert(memoryBus.external[0x1234] == 0x5a);

    TestBus mathBus;
    mathBus.code[0] = 0x75; mathBus.code[1] = 0xe0; mathBus.code[2] = 0x10; // ACC=16
    mathBus.code[3] = 0x75; mathBus.code[4] = 0xf0; mathBus.code[5] = 0x03; // B=3
    mathBus.code[6] = 0xa4; // MUL AB -> 48
    mathBus.code[7] = 0x75; mathBus.code[8] = 0xe0; mathBus.code[9] = 0x10;
    mathBus.code[10] = 0x75; mathBus.code[11] = 0xf0; mathBus.code[12] = 0x03;
    mathBus.code[13] = 0x84; // DIV AB -> quotient 5, remainder 1
    MCS51Core mathCpu;
    for (int i = 0; i < 7; ++i)
        assert(mathCpu.step(mathBus) == MCS51StepResult::executed);
    assert(mathCpu.registers().a == 5);
    assert(mathCpu.registers().b == 1);
    assert(!mathCpu.registers().psw.carry);
    assert(!mathCpu.registers().psw.overflow);

    TestBus divideZeroBus;
    divideZeroBus.code[0] = 0x75; divideZeroBus.code[1] = 0xe0; divideZeroBus.code[2] = 0x25;
    divideZeroBus.code[3] = 0x75; divideZeroBus.code[4] = 0xf0; divideZeroBus.code[5] = 0x00;
    divideZeroBus.code[6] = 0x84;
    MCS51Core divideZeroCpu;
    for (int i = 0; i < 3; ++i)
        assert(divideZeroCpu.step(divideZeroBus) == MCS51StepResult::executed);
    assert(divideZeroCpu.registers().a == 0x25);
    assert(divideZeroCpu.registers().psw.overflow);
    assert(!divideZeroCpu.registers().psw.carry);

    // CJNE sets carry from unsigned comparison and branches only on inequality.
    TestBus cjneBus;
    cjneBus.code[0] = 0x74; cjneBus.code[1] = 0x40; // MOV A,#40
    cjneBus.code[2] = 0xb4; cjneBus.code[3] = 0x42; cjneBus.code[4] = 0x02; // CJNE A,#42,+2
    cjneBus.code[7] = 0x78; cjneBus.code[8] = 0x42; // MOV R0,#42
    cjneBus.code[9] = 0xb8; cjneBus.code[10] = 0x42; cjneBus.code[11] = 0x02; // CJNE R0,#42,+2 (equal)
    MCS51Core cjneCpu;
    assert(cjneCpu.step(cjneBus) == MCS51StepResult::executed);
    assert(cjneCpu.step(cjneBus) == MCS51StepResult::executed);
    assert(cjneCpu.registers().pc == 7);
    assert(cjneCpu.registers().psw.carry);
    assert(cjneCpu.step(cjneBus) == MCS51StepResult::executed);
    assert(cjneCpu.step(cjneBus) == MCS51StepResult::executed);
    assert(cjneCpu.registers().pc == 12);
    assert(!cjneCpu.registers().psw.carry);

    // DJNZ loops exactly until the decremented register reaches zero.
    TestBus djnzBus;
    djnzBus.code[0] = 0x79; djnzBus.code[1] = 0x02; // MOV R1,#2
    djnzBus.code[2] = 0xd9; djnzBus.code[3] = 0xfe; // DJNZ R1,-2
    MCS51Core djnzCpu;
    assert(djnzCpu.step(djnzBus) == MCS51StepResult::executed);
    assert(djnzCpu.step(djnzBus) == MCS51StepResult::executed);
    assert(djnzCpu.registers().pc == 2);
    assert(djnzBus.internal[1] == 1);
    assert(djnzCpu.step(djnzBus) == MCS51StepResult::executed);
    assert(djnzCpu.registers().pc == 4);
    assert(djnzBus.internal[1] == 0);

    // XCH/XCHD preserve the untouched nibble and exchange the intended data.
    TestBus exchangeBus;
    exchangeBus.code[0] = 0x78; exchangeBus.code[1] = 0x40; // MOV R0,#40
    exchangeBus.code[2] = 0x74; exchangeBus.code[3] = 0xaa; // MOV A,#AA
    exchangeBus.code[4] = 0xc6; // XCH A,@R0
    exchangeBus.code[5] = 0x74; exchangeBus.code[6] = 0xab; // MOV A,#AB
    exchangeBus.code[7] = 0xd6; // XCHD A,@R0
    exchangeBus.internal[0x40] = 0x55;
    MCS51Core exchangeCpu;
    for (int i = 0; i < 3; ++i)
        assert(exchangeCpu.step(exchangeBus) == MCS51StepResult::executed);
    assert(exchangeCpu.registers().a == 0x55);
    assert(exchangeBus.internal[0x40] == 0xaa);
    assert(exchangeCpu.step(exchangeBus) == MCS51StepResult::executed);
    assert(exchangeCpu.step(exchangeBus) == MCS51StepResult::executed);
    assert(exchangeCpu.registers().a == 0xaa);
    assert(exchangeBus.internal[0x40] == 0xab);

    // Decimal adjust after packed-BCD addition: 15 + 27 -> 42.
    TestBus decimalBus;
    decimalBus.code[0] = 0x74; decimalBus.code[1] = 0x15;
    decimalBus.code[2] = 0x24; decimalBus.code[3] = 0x27;
    decimalBus.code[4] = 0xd4; // DA A
    MCS51Core decimalCpu;
    for (int i = 0; i < 3; ++i)
        assert(decimalCpu.step(decimalBus) == MCS51StepResult::executed);
    assert(decimalCpu.registers().a == 0x42);

    // MOV transfer forms between direct, register and indirect operands.
    TestBus moveBus;
    moveBus.code[0] = 0x78; moveBus.code[1] = 0x40; // MOV R0,#40
    moveBus.code[2] = 0x75; moveBus.code[3] = 0x30; moveBus.code[4] = 0x5a; // direct 30=5A
    moveBus.code[5] = 0x86; moveBus.code[6] = 0x31; // MOV direct 31,@R0 (internal[40] initially 00)
    moveBus.code[7] = 0xa6; moveBus.code[8] = 0x30; // MOV @R0,direct 30
    moveBus.code[9] = 0x88; moveBus.code[10] = 0x32; // MOV direct 32,R0
    moveBus.code[11] = 0xa8; moveBus.code[12] = 0x30; // MOV R0,direct 30
    MCS51Core moveCpu;
    for (int i = 0; i < 6; ++i)
        assert(moveCpu.step(moveBus) == MCS51StepResult::executed);
    assert(moveBus.internal[0x31] == 0x00);
    assert(moveBus.internal[0x40] == 0x5a);
    assert(moveBus.internal[0x32] == 0x40);
    assert(moveBus.internal[0] == 0x5a);

    // Architectural SFR mirrors, including the parity bit, stay coherent.
    TestBus parityBus;
    parityBus.code[0] = 0x74; parityBus.code[1] = 0x07;
    MCS51Core parityCpu;
    assert(parityCpu.step(parityBus) == MCS51StepResult::executed);
    assert(parityBus.internal[0xe0] == 0x07);
    assert(parityBus.internal[0xd0] == parityCpu.registers().psw.pack());
    assert((parityBus.internal[0xd0] & 1u) == 1u);

    // AJMP/ACALL use the 11-bit page-relative destination encoding.
    TestBus pageJumpBus;
    pageJumpBus.code[0] = 0xe1; pageJumpBus.code[1] = 0x34; // AJMP 0734
    MCS51Core pageJumpCpu;
    assert(pageJumpCpu.step(pageJumpBus) == MCS51StepResult::executed);
    assert(pageJumpCpu.registers().pc == 0x0734);

    TestBus pageCallBus;
    pageCallBus.code[0] = 0x11; pageCallBus.code[1] = 0x56; // ACALL 0056
    pageCallBus.code[0x56] = 0x22; // RET
    MCS51Core pageCallCpu;
    assert(pageCallCpu.step(pageCallBus) == MCS51StepResult::executed);
    assert(pageCallCpu.registers().pc == 0x0056);
    assert(pageCallCpu.registers().sp == 0x09);
    assert(pageCallCpu.step(pageCallBus) == MCS51StepResult::executed);
    assert(pageCallCpu.registers().pc == 0x0002);
    assert(pageCallCpu.registers().sp == 0x07);

    // Indexed MOVX uses P2 as the high address byte and Ri as the low byte.
    TestBus indexedXdataBus;
    indexedXdataBus.internal[0xa0] = 0xee; // P2 pin state differs from latch
    indexedXdataBus.portLatches[2] = 0x12; // P2 output latch supplies MOVX high byte
    indexedXdataBus.internal[0] = 0x34; // R0
    indexedXdataBus.internal[1] = 0x35; // R1
    indexedXdataBus.external[0x1234] = 0x6c;
    indexedXdataBus.code[0] = 0xe2; // MOVX A,@R0
    indexedXdataBus.code[1] = 0xf3; // MOVX @R1,A
    MCS51Core indexedXdataCpu;
    assert(indexedXdataCpu.step(indexedXdataBus) == MCS51StepResult::executed);
    assert(indexedXdataCpu.registers().a == 0x6c);
    assert(indexedXdataCpu.step(indexedXdataBus) == MCS51StepResult::executed);
    assert(indexedXdataBus.external[0x1235] == 0x6c);

    // Carry-only instructions modify CY without disturbing accumulator data.
    TestBus carryOpsBus;
    carryOpsBus.code[0] = 0xd3; // SETB C
    carryOpsBus.code[1] = 0xb3; // CPL C
    carryOpsBus.code[2] = 0xc3; // CLR C
    MCS51Core carryOpsCpu;
    assert(carryOpsCpu.step(carryOpsBus) == MCS51StepResult::executed);
    assert(carryOpsCpu.registers().psw.carry);
    assert(carryOpsCpu.step(carryOpsBus) == MCS51StepResult::executed);
    assert(!carryOpsCpu.registers().psw.carry);
    assert(carryOpsCpu.step(carryOpsBus) == MCS51StepResult::executed);
    assert(!carryOpsCpu.registers().psw.carry);

    // Exhaustive legal-opcode decode smoke test: every classic 8051 opcode
    // must be recognized; 0xA5 is the sole reserved opcode.
    for (unsigned opcode = 0; opcode < 256; ++opcode) {
        if (opcode == 0xa5u) continue;
        TestBus decodeBus;
        decodeBus.code[0] = static_cast<std::uint8_t>(opcode);
        MCS51Core decodeCpu;
        assert(decodeCpu.step(decodeBus) == MCS51StepResult::executed);
    }

    // Exhaustive 8-bit arithmetic flag verification for ADD, ADDC and SUBB.
    // This checks result, CY, AC and signed overflow across every operand pair.
    TestBus arithmeticSweepBus;
    MCS51Core arithmeticSweepCpu;
    for (unsigned a = 0; a < 256; ++a) {
        for (unsigned b = 0; b < 256; ++b) {
            arithmeticSweepCpu.reset();
            arithmeticSweepCpu.registersForTesting().a = static_cast<std::uint8_t>(a);
            arithmeticSweepBus.code[0] = 0x24; // ADD A,#data
            arithmeticSweepBus.code[1] = static_cast<std::uint8_t>(b);
            assert(arithmeticSweepCpu.step(arithmeticSweepBus) == MCS51StepResult::executed);
            const unsigned sum = a + b;
            const auto addResult = static_cast<std::uint8_t>(sum);
            const bool addOverflow = ((~(a ^ b) & (a ^ addResult) & 0x80u) != 0);
            assert(arithmeticSweepCpu.registers().a == addResult);
            assert(arithmeticSweepCpu.registers().psw.carry == (sum > 0xffu));
            assert(arithmeticSweepCpu.registers().psw.auxiliaryCarry == (((a & 0x0fu) + (b & 0x0fu)) > 0x0fu));
            assert(arithmeticSweepCpu.registers().psw.overflow == addOverflow);

            for (unsigned carryIn = 0; carryIn < 2; ++carryIn) {
                arithmeticSweepCpu.reset();
                arithmeticSweepCpu.registersForTesting().a = static_cast<std::uint8_t>(a);
                arithmeticSweepCpu.registersForTesting().psw.carry = carryIn != 0;
                arithmeticSweepBus.code[0] = 0x34; // ADDC A,#data
                assert(arithmeticSweepCpu.step(arithmeticSweepBus) == MCS51StepResult::executed);
                const unsigned addcSum = a + b + carryIn;
                const auto addcResult = static_cast<std::uint8_t>(addcSum);
                const bool addcOverflow = ((~(a ^ b) & (a ^ addcResult) & 0x80u) != 0);
                assert(arithmeticSweepCpu.registers().a == addcResult);
                assert(arithmeticSweepCpu.registers().psw.carry == (addcSum > 0xffu));
                assert(arithmeticSweepCpu.registers().psw.auxiliaryCarry == (((a & 0x0fu) + (b & 0x0fu) + carryIn) > 0x0fu));
                assert(arithmeticSweepCpu.registers().psw.overflow == addcOverflow);

                arithmeticSweepCpu.reset();
                arithmeticSweepCpu.registersForTesting().a = static_cast<std::uint8_t>(a);
                arithmeticSweepCpu.registersForTesting().psw.carry = carryIn != 0;
                arithmeticSweepBus.code[0] = 0x94; // SUBB A,#data
                assert(arithmeticSweepCpu.step(arithmeticSweepBus) == MCS51StepResult::executed);
                const unsigned subtrahend = b + carryIn;
                const auto subResult = static_cast<std::uint8_t>(a - subtrahend);
                const bool subOverflow = (((a ^ b) & (a ^ subResult) & 0x80u) != 0);
                assert(arithmeticSweepCpu.registers().a == subResult);
                assert(arithmeticSweepCpu.registers().psw.carry == (a < subtrahend));
                assert(arithmeticSweepCpu.registers().psw.auxiliaryCarry == ((a & 0x0fu) < ((b & 0x0fu) + carryIn)));
                assert(arithmeticSweepCpu.registers().psw.overflow == subOverflow);
            }
        }
    }

    // Peripheral bus receives exact 8051 machine-cycle advances.
    TestBus cycleBus;
    cycleBus.code[0] = 0x00; // NOP: 1 machine cycle
    cycleBus.code[1] = 0x90; cycleBus.code[2] = 0x12; cycleBus.code[3] = 0x34; // MOV DPTR,#1234: 2
    cycleBus.code[4] = 0x12; cycleBus.code[5] = 0x00; cycleBus.code[6] = 0x10; // LCALL: 2
    MCS51Core cycleCpu;
    assert(cycleCpu.step(cycleBus) == MCS51StepResult::executed);
    assert(cycleCpu.step(cycleBus) == MCS51StepResult::executed);
    assert(cycleCpu.step(cycleBus) == MCS51StepResult::executed);
    assert(cycleCpu.totalCycles() == 5);
    assert(cycleBus.advancedMachineCycles == 5);

    // External interrupt 1 uses vector 0x0013, pushes PC and returns via RETI.
    TestBus interruptBus;
    interruptBus.internal[0xa8] = 0x84; // EA + EX1
    interruptBus.internal[0xb8] = 0x04; // EX1 high priority
    interruptBus.internal[0x88] = 0x0c; // IT1 edge mode + IE1 pending
    interruptBus.code[0x0013] = 0x32; // RETI
    MCS51Core interruptCpu;
    interruptCpu.registersForTesting().pc = 0x1234;
    assert(interruptCpu.step(interruptBus) == MCS51StepResult::executed);
    assert(interruptCpu.registers().pc == 0x0013);
    assert(interruptCpu.registers().sp == 0x09);
    assert(interruptBus.internal[0x08] == 0x34);
    assert(interruptBus.internal[0x09] == 0x12);
    assert((interruptBus.internal[0x88] & 0x08u) == 0);
    assert(interruptCpu.totalCycles() == 2);
    assert(interruptBus.advancedMachineCycles == 2);
    assert(interruptCpu.step(interruptBus) == MCS51StepResult::executed);
    assert(interruptCpu.registers().pc == 0x1234);
    assert(interruptCpu.registers().sp == 0x07);

    // A high-priority timer interrupt may pre-empt a low-priority external ISR.
    TestBus nestedInterruptBus;
    nestedInterruptBus.internal[0xa8] = 0x86; // EA + EX1 + ET0
    nestedInterruptBus.internal[0xb8] = 0x02; // Timer 0 high, EX1 low
    nestedInterruptBus.internal[0x88] = 0x0c; // edge-triggered INT1 pending
    nestedInterruptBus.code[0x000b] = 0x32; // high-priority RETI
    nestedInterruptBus.code[0x0013] = 0x32; // low-priority RETI
    MCS51Core nestedInterruptCpu;
    nestedInterruptCpu.registersForTesting().pc = 0x4567;
    assert(nestedInterruptCpu.step(nestedInterruptBus) == MCS51StepResult::executed);
    assert(nestedInterruptCpu.registers().pc == 0x0013);
    nestedInterruptBus.internal[0x88] |= 0x20; // Timer 0 overflow while INT1 ISR is active
    assert(nestedInterruptCpu.step(nestedInterruptBus) == MCS51StepResult::executed);
    assert(nestedInterruptCpu.registers().pc == 0x000b);
    assert((nestedInterruptBus.internal[0x88] & 0x20u) == 0); // TF0 auto-cleared on entry
    assert(nestedInterruptCpu.step(nestedInterruptBus) == MCS51StepResult::executed);
    assert(nestedInterruptCpu.registers().pc == 0x0013);
    assert(nestedInterruptCpu.step(nestedInterruptBus) == MCS51StepResult::executed);
    assert(nestedInterruptCpu.registers().pc == 0x4567);
    assert(nestedInterruptCpu.registers().sp == 0x07);

    // Intel 8032 adds Timer 2 at vector 0x002B; TF2/EXF2 remain software-cleared.
    TestBus timer2InterruptBus;
    timer2InterruptBus.internal[0xa8] = 0xa0; // EA + ET2
    timer2InterruptBus.internal[0xb8] = 0x20; // PT2 high priority
    timer2InterruptBus.internal[0xc8] = 0xc0; // TF2 + EXF2
    timer2InterruptBus.code[0x002b] = 0x32; // RETI
    MCS51Core timer2InterruptCpu;
    timer2InterruptCpu.registersForTesting().pc = 0x2345;
    assert(timer2InterruptCpu.step(timer2InterruptBus) == MCS51StepResult::executed);
    assert(timer2InterruptCpu.registers().pc == 0x002b);
    assert(timer2InterruptBus.internal[0xc8] == 0xc0); // firmware must clear Timer 2 flags
    assert(timer2InterruptCpu.step(timer2InterruptBus) == MCS51StepResult::executed);
    assert(timer2InterruptCpu.registers().pc == 0x2345);

    // On the 8032, indirect 80h-FFh accesses upper IRAM while direct
    // accesses at the same addresses still select SFRs.
    TestBus upperIramBus;
    upperIramBus.internal[0x00] = 0x80; // R0 points into upper IRAM
    upperIramBus.internal[0x80] = 0xa5; // P0 SFR value
    upperIramBus.code[0] = 0x76; upperIramBus.code[1] = 0x66; // MOV @R0,#66
    upperIramBus.code[2] = 0xe6; // MOV A,@R0
    MCS51Core upperIramCpu;
    assert(upperIramCpu.step(upperIramBus) == MCS51StepResult::executed);
    assert(upperIramCpu.step(upperIramBus) == MCS51StepResult::executed);
    assert(upperIramBus.upperInternal[0] == 0x66);
    assert(upperIramBus.internal[0x80] == 0xa5);
    assert(upperIramCpu.registers().a == 0x66);

    // Stack pointers crossing 7Fh use upper IRAM, not SFR addresses.
    TestBus upperStackBus;
    upperStackBus.code[0x1234] = 0x12; // LCALL 2000
    upperStackBus.code[0x1235] = 0x20;
    upperStackBus.code[0x1236] = 0x00;
    upperStackBus.code[0x2000] = 0x22; // RET
    MCS51Core upperStackCpu;
    upperStackCpu.registersForTesting().pc = 0x1234;
    upperStackCpu.registersForTesting().sp = 0x7f;
    assert(upperStackCpu.step(upperStackBus) == MCS51StepResult::executed);
    assert(upperStackCpu.registers().sp == 0x81);
    assert(upperStackBus.upperInternal[0] == 0x37);
    assert(upperStackBus.upperInternal[1] == 0x12);
    assert(upperStackBus.internal[0x81] == 0x81); // SP SFR mirror
    assert(upperStackCpu.step(upperStackBus) == MCS51StepResult::executed);
    assert(upperStackCpu.registers().pc == 0x1237);
    assert(upperStackCpu.registers().sp == 0x7f);

    // Read-modify-write operations use the port output latch, while JB reads pins.
    TestBus portRmwBus;
    portRmwBus.internal[0x90] = 0xf0; // P1 pin state
    portRmwBus.portLatches[1] = 0x0f; // P1 output latch
    portRmwBus.code[0] = 0x53; // ANL P1,#0xF0
    portRmwBus.code[1] = 0x90;
    portRmwBus.code[2] = 0xf0;
    MCS51Core portRmwCpu;
    assert(portRmwCpu.step(portRmwBus) == MCS51StepResult::executed);
    assert(portRmwBus.portLatches[1] == 0x00); // latch 0F & F0, not pin F0 & F0

    TestBus portPinBranchBus;
    portPinBranchBus.internal[0x90] = 0x10; // P1.4 pin high
    portPinBranchBus.portLatches[1] = 0x00; // output latch low
    portPinBranchBus.code[0] = 0x20; // JB P1.4,+2
    portPinBranchBus.code[1] = 0x94;
    portPinBranchBus.code[2] = 0x02;
    MCS51Core portPinBranchCpu;
    assert(portPinBranchCpu.step(portPinBranchBus) == MCS51StepResult::executed);
    assert(portPinBranchCpu.registers().pc == 0x0005); // branch follows pin, not latch

    TestBus portJbcBus;
    portJbcBus.internal[0x90] = 0x00; // P1.4 pin low
    portJbcBus.portLatches[1] = 0x10; // P1.4 latch high
    portJbcBus.code[0] = 0x10; // JBC P1.4,+2
    portJbcBus.code[1] = 0x94;
    portJbcBus.code[2] = 0x02;
    MCS51Core portJbcCpu;
    assert(portJbcCpu.step(portJbcBus) == MCS51StepResult::executed);
    assert(portJbcCpu.registers().pc == 0x0005); // JBC tests latch
    assert(portJbcBus.portLatches[1] == 0x00); // and clears that latch bit

    TestBus portCplBus;
    portCplBus.internal[0x90] = 0x10; // P1.4 pin high
    portCplBus.portLatches[1] = 0x00; // P1.4 latch low
    portCplBus.code[0] = 0xb2; // CPL P1.4
    portCplBus.code[1] = 0x94;
    MCS51Core portCplCpu;
    assert(portCplCpu.step(portCplBus) == MCS51StepResult::executed);
    assert(portCplBus.portLatches[1] == 0x10); // CPL operates on latch

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
