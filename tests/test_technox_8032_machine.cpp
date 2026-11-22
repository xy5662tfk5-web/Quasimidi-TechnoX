#include "../Source/TechnoX8032Machine.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
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

    // Full 16 KiB external sound-memory backing store, with SFR 8E in mode 2.
    machine.reset();
    machine.writeInternal(0x8e, 0x02);
    for (unsigned i = 0; i < 0x4000; ++i) {
        const auto address = static_cast<std::uint16_t>(0xc000u + i);
        const auto value = static_cast<std::uint8_t>((i * 37u) ^ 0xa5u);
        machine.writeExternal(address, value);
    }
    for (unsigned i = 0; i < 0x4000; ++i) {
        const auto address = static_cast<std::uint16_t>(0xc000u + i);
        const auto expected = static_cast<std::uint8_t>((i * 37u) ^ 0xa5u);
        assert(machine.readExternal(address) == expected);
    }
    assert(machine.soundMemoryWritesByMode(2) == 0x4000u);
    assert(machine.soundMemoryReadsByMode(2) == 0x4000u);
    // The same FF10 address is a sound-device status/data port in other bus modes.
    machine.writeInternal(0x8e, 0x01);
    machine.writeExternal(0xff10, 0x5a);
    assert(machine.lastSoundDeviceWrite() == 0x5a);
    assert(machine.readExternal(0xff10) == 0xff);

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
    machine.writeExternal(0xa000, 0x96); // divide-by-64, 8N1, receive interrupt enabled
    machine.injectAciaReceiveByte(0x6c);
    assert((machine.readInternal(0x88) & 0x08u) == 0); // pin sampled on machine cycle
    machine.advanceMachineCycles(1);
    assert((machine.readInternal(0x88) & 0x08u) != 0); // TCON.IE1 level request
    assert((machine.readExternal(0xa002) & 0x81u) == 0x81u); // IRQ + RDRF
    assert(machine.readExternal(0xa003) == 0x6c);
    assert((machine.readExternal(0xa002) & 0x81u) == 0); // data read clears IRQ
    machine.advanceMachineCycles(1);
    assert((machine.readInternal(0x88) & 0x08u) == 0);

    machine.writeExternal(0xa001, 0x7d);
    std::uint8_t transmitted = 0;
    assert((machine.readExternal(0xa002) & 0x02u) == 0); // TDRE clears on write
    assert(machine.takeAciaTransmitByte(transmitted));
    assert(transmitted == 0x7d);
    assert(!machine.takeAciaTransmitByte(transmitted));
    const auto txBitCycles = static_cast<unsigned>(machine.midiByteMachineCycles() / 10u);
    machine.advanceMachineCycles(txBitCycles - 1u);
    assert((machine.readExternal(0xa002) & 0x02u) == 0);
    machine.advanceMachineCycles(1);
    assert((machine.readExternal(0xa002) & 0x02u) != 0); // TDRE after one TX clock

    // MIDI bytes arrive over the ACIA at 31.25 kbaud (10 serial bits per byte).
    machine.reset();
    machine.writeExternal(0xa000, 0x96); // ACIA receive interrupt enabled
    const std::array<std::uint8_t, 3> midiBytes{{0x90, 0x3c, 0x64}};
    const auto midiStart = machine.elapsedMachineCycles();
    assert(machine.scheduleAciaMidiBytes(midiBytes.data(), midiBytes.size(), midiStart));
    const auto midiByteCycles = machine.midiByteMachineCycles();
    machine.advanceMachineCycles(static_cast<unsigned>(midiByteCycles - 1u));
    assert((machine.readExternal(0xa002) & 0x01u) == 0);
    machine.advanceMachineCycles(1);
    assert((machine.readExternal(0xa002) & 0x01u) != 0);
    assert((machine.readInternal(0xb0) & 0x08u) == 0); // INT1 active low
    assert(machine.readExternal(0xa003) == 0x90);
    machine.advanceMachineCycles(static_cast<unsigned>(midiByteCycles - 1u));
    assert((machine.readExternal(0xa002) & 0x01u) == 0);
    machine.advanceMachineCycles(1);
    assert((machine.readExternal(0xa002) & 0x01u) != 0);
    assert(machine.readExternal(0xa003) == 0x3c);
    machine.advanceMachineCycles(static_cast<unsigned>(midiByteCycles));
    assert((machine.readExternal(0xa002) & 0x01u) != 0);
    assert(machine.readExternal(0xa003) == 0x64);

    // External sound-device port: preserve observed bus writes, but do not
    // synthesize the undocumented 0xAC ready response from unrelated RAM writes.
    machine.reset();
    machine.writeInternal(0xb0, 0xfb); // observed active-low P3.2 reset transition
    machine.writeExternal(0xff10, 0xff);
    assert(machine.lastSoundDeviceWrite() == 0xff);
    machine.writeExternal(0xc000, 0x55);
    machine.writeInternal(0xb0, 0xff); // release reset; response remains unknown
    assert(machine.readExternal(0xff10) == 0xff);
    machine.setSoundDeviceResponseForTesting(0xac); // explicit peripheral test fixture
    assert(machine.readExternal(0xff10) == 0xac);

    // INT0 level mode follows the active-low P3.2 input.
    machine.reset();
    machine.writeInternal(0x88, 0x00); // IT0 level-triggered
    machine.setPortPins(0xb0, 0xfb); // INT0 low
    machine.advanceMachineCycles(1);
    assert((machine.readInternal(0x88) & 0x02u) != 0); // IE0
    machine.setPortPins(0xb0, 0xff); // INT0 released
    machine.advanceMachineCycles(1);
    assert((machine.readInternal(0x88) & 0x02u) == 0);

    // INT0 edge mode latches only the falling edge until firmware clears IE0.
    machine.reset();
    machine.writeInternal(0x88, 0x01); // IT0 edge-triggered
    machine.setPortPins(0xb0, 0xfb);
    machine.advanceMachineCycles(1);
    assert((machine.readInternal(0x88) & 0x02u) != 0);
    machine.writeInternal(0x88, 0x01); // emulate interrupt-entry auto-clear
    machine.advanceMachineCycles(1); // pin remains low; no second falling edge
    assert((machine.readInternal(0x88) & 0x02u) == 0);

    // ACIA's active-low IRQ drives INT1; edge mode does not retrigger while held.
    machine.reset();
    machine.writeInternal(0x88, 0x04); // IT1 edge-triggered
    machine.writeExternal(0xa000, 0x96);
    machine.injectAciaReceiveByte(0x31);
    machine.advanceMachineCycles(1);
    assert((machine.readInternal(0x88) & 0x08u) != 0);
    machine.writeInternal(0x88, 0x04); // emulate INT1 auto-clear
    machine.advanceMachineCycles(1);
    assert((machine.readInternal(0x88) & 0x08u) == 0);
    assert(machine.readExternal(0xa003) == 0x31); // release ACIA IRQ
    machine.advanceMachineCycles(1); // sample released/high pin
    machine.injectAciaReceiveByte(0x32);
    machine.advanceMachineCycles(1);
    assert((machine.readInternal(0x88) & 0x08u) != 0); // next falling edge

    // Timer 0 mode 1: 16-bit overflow raises TF0.
    machine.reset();
    machine.writeInternal(0x89, 0x01); // TMOD: Timer 0 mode 1
    machine.writeInternal(0x8c, 0xff); // TH0
    machine.writeInternal(0x8a, 0xfe); // TL0
    machine.writeInternal(0x88, 0x10); // TR0
    machine.advanceMachineCycles(2);
    assert(machine.readInternal(0x8c) == 0x00);
    assert(machine.readInternal(0x8a) == 0x00);
    assert((machine.readInternal(0x88) & 0x20u) != 0); // TF0

    // Timer 0 mode 2: overflow reloads TL0 from TH0.
    machine.reset();
    machine.writeInternal(0x89, 0x02); // mode 2
    machine.writeInternal(0x8c, 0xa5); // reload
    machine.writeInternal(0x8a, 0xff);
    machine.writeInternal(0x88, 0x10); // TR0
    machine.advanceMachineCycles(1);
    assert(machine.readInternal(0x8a) == 0xa5);
    assert((machine.readInternal(0x88) & 0x20u) != 0);

    // Timer 2 auto-reload and interrupt flag behavior on the 8032.
    machine.reset();
    machine.writeInternal(0xcb, 0x12); // RCAP2H
    machine.writeInternal(0xca, 0x34); // RCAP2L
    machine.writeInternal(0xcd, 0xff); // TH2
    machine.writeInternal(0xcc, 0xff); // TL2
    machine.writeInternal(0xc8, 0x04); // TR2, auto-reload mode
    machine.advanceMachineCycles(1);
    assert(machine.readInternal(0xcd) == 0x12);
    assert(machine.readInternal(0xcc) == 0x34);
    assert((machine.readInternal(0xc8) & 0x80u) != 0); // TF2

    // Timer 0 counter mode increments on a sampled falling edge at T0/P3.4.
    machine.reset();
    machine.writeInternal(0x89, 0x05); // Timer 0 mode 1, C/T0 counter input
    machine.writeInternal(0x88, 0x10); // TR0
    machine.setPortPins(0xb0, 0xef); // T0 transitions high -> low
    machine.advanceMachineCycles(1);
    assert(machine.readInternal(0x8a) == 0x01);

    // Timer 2 capture mode latches TH2/TL2 on a T2EX/P1.1 falling edge.
    machine.reset();
    machine.writeInternal(0xcd, 0x12);
    machine.writeInternal(0xcc, 0x34);
    machine.writeInternal(0xc8, 0x09); // EXEN2 + CP/RL2, stopped
    machine.setPortPins(0x90, 0xfd); // T2EX high -> low
    machine.advanceMachineCycles(1);
    assert(machine.readInternal(0xcb) == 0x12);
    assert(machine.readInternal(0xca) == 0x34);
    assert((machine.readInternal(0xc8) & 0x40u) != 0); // EXF2

    // Timer 2 counter mode increments on a falling edge at T2/P1.0.
    machine.reset();
    machine.writeInternal(0xc8, 0x06); // TR2 + C/T2
    machine.setPortPins(0x90, 0xfe); // T2 high -> low
    machine.advanceMachineCycles(1);
    assert(machine.readInternal(0xcc) == 0x01);

    // Timer 2 baud-rate mode reloads but suppresses TF2.
    machine.reset();
    machine.writeInternal(0xcb, 0x22);
    machine.writeInternal(0xca, 0x11);
    machine.writeInternal(0xcd, 0xff);
    machine.writeInternal(0xcc, 0xff);
    machine.writeInternal(0xc8, 0x24); // TR2 + RCLK
    machine.advanceMachineCycles(1);
    assert(machine.readInternal(0xcd) == 0x22);
    assert(machine.readInternal(0xcc) == 0x11);
    assert((machine.readInternal(0xc8) & 0x80u) == 0);

    // LCD stub and cycle accounting.
    machine.reset();
    machine.writeExternal(0xb001, 0x41);
    assert(machine.readExternal(0xb003) == 0x41);
    machine.advanceMachineCycles(17);
    assert(machine.elapsedMachineCycles() == 17);

    // Optional real-ROM smoke test: verify the descrambled reset vector and
    // execute the original IC4 startup path in the 8032 bus model.
    if (argc > 1) {
        std::ifstream romFile(argv[1], std::ios::binary);
        assert(romFile.good());
        const std::istreambuf_iterator<char> romBegin{romFile};
        const std::istreambuf_iterator<char> romEnd{};
        const std::vector<std::uint8_t> realRom(romBegin, romEnd);
        assert(realRom.size() == TechnoX8032Machine::kIC4RomSize);

        std::vector<std::uint8_t> realIC51;
        if (argc > 2) {
            std::ifstream ic51File(argv[2], std::ios::binary);
            assert(ic51File.good());
            const std::istreambuf_iterator<char> ic51Begin{ic51File};
            const std::istreambuf_iterator<char> ic51End{};
            realIC51.assign(ic51Begin, ic51End);
            assert(realIC51.size() == TechnoX8032Machine::kIC51RomSize);

            TechnoX8032Machine ic51Probe;
            assert(ic51Probe.loadIC51Rom(realIC51.data(), realIC51.size()));
            ic51Probe.reset();
            ic51Probe.writeInternal(0xb0, 0xcdu); // P3.5/P3.4/P3.1 low: bank 0
            assert(ic51Probe.readExternal(0x0000) == realIC51[0x0000]);
            assert(ic51Probe.readExternal(0x1234) == realIC51[0x1234]);
            assert(ic51Probe.ic51ReadsByBank(0) == 2u);
            ic51Probe.writeInternal(0xb0, 0xedu); // P3.5 high: bank 1
            assert(ic51Probe.readExternal(0x0000) == realIC51[0x10000]);
            assert(ic51Probe.readExternal(0x1234) == realIC51[0x11234]);
            assert(ic51Probe.ic51ReadsByBank(1) == 2u);
            ic51Probe.writeInternal(0xb0, 0xffu); // deselect IC51
        }

        TechnoX8032Machine firmwareMachine;
        assert(firmwareMachine.loadIC4Rom(realRom.data(), realRom.size()));
        if (!realIC51.empty())
            assert(firmwareMachine.loadIC51Rom(realIC51.data(), realIC51.size()));
        assert(firmwareMachine.readCode(0x0000) == 0x02); // LJMP reset vector
        assert(firmwareMachine.readCode(0x0001) == 0xeb);
        assert(firmwareMachine.readCode(0x0002) == 0x32);

        // AEDD maps every logical low-byte address through the 256-byte
        // firmware table at ADDD before each external sound-memory write.
        std::array<bool, 256> mappedAddressSeen{};
        assert(firmwareMachine.readCode(0xaddd) == 0x04);
        assert(firmwareMachine.readCode(0xadde) == 0x25);
        assert(firmwareMachine.readCode(0xaddf) == 0x16);
        assert(firmwareMachine.readCode(0xade0) == 0x37);
        for (unsigned i = 0; i < 256; ++i) {
            const auto logical = static_cast<std::uint8_t>(i);
            const auto mapped = firmwareMachine.readCode(
                static_cast<std::uint16_t>(0xadddu + i));
            assert(mapped == TechnoX8032Machine::mapSoundMemoryLowAddress(logical));
            assert(TechnoX8032Machine::unmapSoundMemoryLowAddress(mapped) == logical);
            assert(!mappedAddressSeen[mapped]);
            mappedAddressSeen[mapped] = true;
        }
        firmwareMachine.reset();
        // The reset vector is a two-machine-cycle LJMP to EB32. A budget of
        // one cycle must finish that instruction rather than split it.
        assert(firmwareMachine.runForMachineCycles(1) == 1);
        assert(firmwareMachine.elapsedMachineCycles() == 2);
        assert(firmwareMachine.cpu().registers().pc == 0xeb32);
        firmwareMachine.reset();

        // Seed the firmware's startup-cleared XDATA range and one byte just
        // beyond its documented 0000-34FF initialization loop.
        firmwareMachine.writeExternal(0x0000, 0xa5);
        firmwareMachine.writeExternal(0x34ff, 0xa5);
        firmwareMachine.writeExternal(0x3500, 0xa5);
        const auto executed = firmwareMachine.runUntilProgramCounter(0xeb8d, 50000);
        std::printf("IC4 boot trace: instructions=%zu PC=%04X SP=%02X cycles=%llu\n",
                    executed, static_cast<unsigned>(firmwareMachine.cpu().registers().pc),
                    static_cast<unsigned>(firmwareMachine.cpu().registers().sp),
                    static_cast<unsigned long long>(firmwareMachine.cpu().totalCycles()));
        assert(executed == 41278);
        assert(firmwareMachine.cpu().registers().pc == 0xeb8d);
        assert(firmwareMachine.cpu().registers().sp == 0xb5);
        assert(firmwareMachine.readExternal(0x0000) == 0x00);
        assert(firmwareMachine.readExternal(0x34ff) == 0x00);
        assert(firmwareMachine.readExternal(0x3500) == 0xa5);

        const auto resourceInit = firmwareMachine.runUntilProgramCounter(0x326a, 50000);
        std::printf("IC4 resource-table handoff: instructions=%zu PC=%04X\\n",
                    resourceInit, static_cast<unsigned>(firmwareMachine.cpu().registers().pc));
        assert(resourceInit != 0);
        assert(firmwareMachine.cpu().registers().pc == 0x326a);

        const auto peripheralInit = firmwareMachine.runUntilProgramCounter(0x32b0, 2000000);
        const bool reachedPeripheralHandoff = firmwareMachine.cpu().registers().pc == 0x32b0;
        const auto bank = firmwareMachine.cpu().registers().psw.registerBankBase();
        std::printf("IC4 peripheral-init trace: reached=%u instructions=%zu PC=%04X opcode=%02X SP=%02X R2=%02X R3=%02X R4=%02X R5=%02X R6=%02X R7=%02X P3=%02X lastFF10write=%02X FF10read=%02X XWR=%llu XRD=%llu 8EWR=%llu P35=%llu mode8E=%02X mode2WR=%llu mode2RD=%llu lastMem=%04X:%02X cycles=%llu\\n",
                    static_cast<unsigned>(reachedPeripheralHandoff), peripheralInit,
                    static_cast<unsigned>(firmwareMachine.cpu().registers().pc),
                    static_cast<unsigned>(firmwareMachine.readCode(firmwareMachine.cpu().registers().pc)),
                    static_cast<unsigned>(firmwareMachine.cpu().registers().sp),
                    static_cast<unsigned>(firmwareMachine.readInternal(static_cast<std::uint8_t>(bank + 2u))),
                    static_cast<unsigned>(firmwareMachine.readInternal(static_cast<std::uint8_t>(bank + 3u))),
                    static_cast<unsigned>(firmwareMachine.readInternal(static_cast<std::uint8_t>(bank + 4u))),
                    static_cast<unsigned>(firmwareMachine.readInternal(static_cast<std::uint8_t>(bank + 5u))),
                    static_cast<unsigned>(firmwareMachine.readInternal(static_cast<std::uint8_t>(bank + 6u))),
                    static_cast<unsigned>(firmwareMachine.readInternal(static_cast<std::uint8_t>(bank + 7u))),
                    static_cast<unsigned>(firmwareMachine.readInternal(0xb0)),
                    static_cast<unsigned>(firmwareMachine.lastSoundDeviceWrite()),
                    static_cast<unsigned>(firmwareMachine.readExternal(0xff10)),
                    static_cast<unsigned long long>(firmwareMachine.soundMemoryWriteCount()),
                    static_cast<unsigned long long>(firmwareMachine.soundMemoryReadCount()),
                    static_cast<unsigned long long>(firmwareMachine.soundBusModeWriteCount()),
                    static_cast<unsigned long long>(firmwareMachine.p35TransitionCount()),
                    static_cast<unsigned>(firmwareMachine.externalSoundBusMode()),
                    static_cast<unsigned long long>(firmwareMachine.soundMemoryWritesByMode(2)),
                    static_cast<unsigned long long>(firmwareMachine.soundMemoryReadsByMode(2)),
                    static_cast<unsigned>(firmwareMachine.lastSoundMemoryWriteAddress()),
                    static_cast<unsigned>(firmwareMachine.lastSoundMemoryWriteValue()),
                    static_cast<unsigned long long>(firmwareMachine.cpu().totalCycles()));
        std::fflush(stdout);
        assert(peripheralInit > 0);
        assert(firmwareMachine.soundMemoryWriteCount() > 0u);
        assert(firmwareMachine.p35TransitionCount() > 0u);
        assert(firmwareMachine.cpu().totalCycles() != 0);

        // Controlled test stimulus only: inject the expected AC signature so
        // the CPU can pass the otherwise-unmodeled external-device wait.
        TechnoX8032Machine injectedResponseMachine;
        assert(injectedResponseMachine.loadIC4Rom(
            realRom.data(),
            realRom.size()));
        if (!realIC51.empty())
            assert(injectedResponseMachine.loadIC51Rom(realIC51.data(), realIC51.size()));
        injectedResponseMachine.reset();
        injectedResponseMachine.writeExternal(0x0000, 0xa5);
        injectedResponseMachine.writeExternal(0x34ff, 0xa5);
        injectedResponseMachine.writeExternal(0x3500, 0xa5);
        assert(injectedResponseMachine.runUntilProgramCounter(0xeb8d, 50000) == 41278);
        assert(injectedResponseMachine.runUntilProgramCounter(0x326a, 50000) != 0);
        injectedResponseMachine.setSoundDeviceResponseForTesting(0xac);
        const auto injectedBoot = injectedResponseMachine.runUntilProgramCounter(0x32b0, 2000000);
        std::printf("IC4 injected-response diagnostic (test-only AC): instructions=%zu PC=%04X XWR=%llu XRD=%llu mode2WR=%llu mode2RD=%llu\\n",
                    injectedBoot,
                    static_cast<unsigned>(injectedResponseMachine.cpu().registers().pc),
                    static_cast<unsigned long long>(injectedResponseMachine.soundMemoryWriteCount()),
                    static_cast<unsigned long long>(injectedResponseMachine.soundMemoryReadCount()),
                    static_cast<unsigned long long>(injectedResponseMachine.soundMemoryWritesByMode(2)),
                    static_cast<unsigned long long>(injectedResponseMachine.soundMemoryReadsByMode(2)));
        assert(injectedResponseMachine.cpu().registers().pc == 0x32b0);
        assert(injectedResponseMachine.soundMemoryWriteCount() > 0u);

        // Exercise the real INT1/ACIA receive path with a MIDI Note On.
        const std::array<std::uint8_t, 3> noteOn{{0x90, 0x3c, 0x64}};
        const auto midiStartCycle = injectedResponseMachine.elapsedMachineCycles();
        const auto ic51ReadsBeforeMidi = injectedResponseMachine.ic51ReadCount();
        const auto ieBeforeMidi = injectedResponseMachine.readInternal(0xa8);
        assert(injectedResponseMachine.scheduleAciaMidiBytes(
            noteOn.data(), noteOn.size(), midiStartCycle));
        const auto midiInstructions = injectedResponseMachine.runForMachineCycles(100000);
        std::printf("IC4 MIDI Note On diagnostic: instructions=%zu PC=%04X IE=%02X TCON=%02X ACIA=%02X P3=%02X IC51=%llu bank0=%llu bank1=%llu lastIC51=%04X\\n",
                    midiInstructions,
                    static_cast<unsigned>(injectedResponseMachine.cpu().registers().pc),
                    static_cast<unsigned>(ieBeforeMidi),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x88)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0xa002)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0xb0)),
                    static_cast<unsigned long long>(injectedResponseMachine.ic51ReadCount()),
                    static_cast<unsigned long long>(injectedResponseMachine.ic51ReadsByBank(0)),
                    static_cast<unsigned long long>(injectedResponseMachine.ic51ReadsByBank(1)),
                    static_cast<unsigned>(injectedResponseMachine.lastIC51ReadAddress()));
        assert(!injectedResponseMachine.cpuFaulted());
        (void)ic51ReadsBeforeMidi;
    }
    return 0;
}
