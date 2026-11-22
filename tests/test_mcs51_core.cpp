#include "../Source/MCS51Core.h"

#include <cassert>
#include <cstdint>

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

    regs.pc = 0xeb32;
    regs.a = 0x55;
    cpu.reset();
    assert(cpu.registers().pc == 0);
    assert(cpu.registers().a == 0);
    return 0;
}
