#include "../Source/TechnoX8032Machine.h"

#include <array>
#include <cassert>
#include <cstdint>

int main() {
    using technox::TechnoX8032Machine;

    TechnoX8032Machine machine;
    std::array<std::uint8_t, TechnoX8032Machine::kIC4RomSize> raw{};
    for (std::size_t i = 0; i < raw.size(); ++i)
        raw[i] = static_cast<std::uint8_t>(i & 0xffu);

    assert(!machine.loadIC4Rom(raw.data(), raw.size() - 1));
    assert(machine.loadIC4Rom(raw.data(), raw.size()));
    assert(machine.romLoaded());
    assert(machine.readCode(0x0000) == TechnoX8032Machine::descrambledOffset(0x00));
    assert(machine.readCode(0x0001) == TechnoX8032Machine::descrambledOffset(0x01));
    assert(machine.readCode(0xffff) == TechnoX8032Machine::descrambledOffset(0xff));

    machine.reset();
    assert(machine.cpu().registers().pc == 0);
    assert(machine.cpu().registers().sp == 0x07);

    // Firmware-visible RAM ranges: lower XDATA and the 16 KiB startup-test window.
    machine.writeExternal(0x0000, 0x12);
    machine.writeExternal(0x38ff, 0x34);
    machine.writeExternal(0xc000, 0x56);
    machine.writeExternal(0xffff, 0x78);
    assert(machine.readExternal(0x0000) == 0x12);
    assert(machine.readExternal(0x38ff) == 0x34);
    assert(machine.readExternal(0xc000) == 0x56);
    assert(machine.readExternal(0xffff) == 0x78);

    // Unmapped/stub windows remain open bus instead of being fabricated as RAM.
    machine.writeExternal(0x3900, 0xaa);
    machine.writeExternal(0x7c00, 0xbb);
    assert(machine.readExternal(0x3900) == 0xff);
    assert(machine.readExternal(0x7c00) == 0xff);

    // Four-byte hardware command window is captured for later protocol analysis.
    machine.writeExternal(0x8000, 0x11);
    machine.writeExternal(0x8001, 0x22);
    machine.writeExternal(0x8002, 0x33);
    machine.writeExternal(0x8003, 0x44);
    const auto& command = machine.commandPortBytes();
    assert(command[0] == 0x11 && command[1] == 0x22);
    assert(command[2] == 0x33 && command[3] == 0x44);
    assert(machine.readExternal(0x8000) == 0xff);

    // 8032 upper IRAM and direct SFR reads are separate address spaces.
    machine.writeIndirect(0x80, 0x5a);
    machine.writeInternal(0x80, 0xa5);
    assert(machine.readIndirect(0x80) == 0x5a);
    assert(machine.readInternal(0x80) == 0xa5);

    // P2 pin reads and P2 output-latch reads are distinct.
    machine.writeInternal(0xa0, 0x12);
    machine.setPortPins(0xa0, 0xee);
    assert(machine.readInternal(0xa0) == 0xee);
    assert(machine.readPortLatch(0xa0) == 0x12);

    // ACIA register map, receive data and its INT1 request.
    machine.writeExternal(0xa000, 0x80); // receive interrupt enabled
    machine.injectAciaReceiveByte(0x6c);
    assert((machine.readInternal(0x88) & 0x08u) != 0); // TCON.IE1
    assert((machine.readExternal(0xa002) & 0x01u) != 0); // RDRF
    assert(machine.readExternal(0xa003) == 0x6c);
    assert((machine.readExternal(0xa002) & 0x01u) == 0);
    assert((machine.readInternal(0x88) & 0x08u) == 0);

    machine.writeExternal(0xa001, 0x7d);
    std::uint8_t transmitted = 0;
    assert(machine.takeAciaTransmitByte(transmitted));
    assert(transmitted == 0x7d);
    assert(!machine.takeAciaTransmitByte(transmitted));

    // LCD stub and cycle accounting.
    machine.writeExternal(0xb001, 0x41);
    assert(machine.readExternal(0xb003) == 0x41);
    machine.advanceMachineCycles(17);
    assert(machine.elapsedMachineCycles() == 17);
    return 0;
}
