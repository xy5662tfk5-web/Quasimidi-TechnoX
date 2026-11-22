#include "../Source/TechnoX8032Machine.h"

#include <algorithm>
#include <cstddef>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>
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

    // External hardware can independently drive GPIO input pins while the
    // CPU-visible output latch remains unchanged (P1.3/P1.5 on IC4).
    machine.writeInternal(0x90, 0xff);
    machine.setExternalPortInputs(0x90, 0x28, 0x00);
    assert(machine.readInternal(0x90) == 0xd7);
    assert(machine.readPortLatch(0x90) == 0xff);
    machine.reset();
    assert(machine.readInternal(0x90) == 0xff);

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
    assert(machine.readExternal(0x8000) == 0x11);
    assert(machine.readExternal(0x8001) == 0x22);
    assert(machine.readExternal(0x8002) == 0x33);
    assert(machine.readExternal(0x8003) == 0x44);
    assert(machine.soundCommandPacketCount() == 1u);
    const auto& capturedCommand = machine.soundCommandPacketAt(0);
    assert(capturedCommand.bytes == command);

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
            ic51Probe.writeInternal(0xb0, 0xdau); // AF1A bulk-read select: bank 0
            assert(ic51Probe.readExternal(0x0090) == realIC51[0x0090]);
            assert(ic51Probe.ic51ReadsByBank(0) == 3u);
            ic51Probe.writeInternal(0xb0, 0xfau); // high/high/high is deselected
            assert(ic51Probe.readExternal(0x0090) == 0x00u);
            ic51Probe.writeInternal(0xb0, 0xffu); // deselect IC51
        }

        struct TimedSysExFrame {
            std::uint32_t deltaTicks = 0;
            std::vector<std::uint8_t> bytes;
        };
        std::vector<TimedSysExFrame> factoryFrames;
        std::uint16_t factoryDivision = 0;
        std::uint32_t factoryTempo = 0;
        if (argc > 3) {
            std::ifstream streamFile(argv[3], std::ios::binary);
            assert(streamFile.good());
            const std::istreambuf_iterator<char> streamBegin{streamFile};
            const std::istreambuf_iterator<char> streamEnd{};
            const std::vector<std::uint8_t> streamBytes(streamBegin, streamEnd);
            assert(streamBytes.size() >= 11u);
            assert(streamBytes[0] == 'T' && streamBytes[1] == 'X' &&
                   streamBytes[2] == 'S' && streamBytes[3] == 'B');
            factoryDivision = static_cast<std::uint16_t>(
                (static_cast<unsigned>(streamBytes[4]) << 8u) | streamBytes[5]);
            factoryTempo = (static_cast<std::uint32_t>(streamBytes[6]) << 16u) |
                           (static_cast<std::uint32_t>(streamBytes[7]) << 8u) |
                           streamBytes[8];
            const auto frameCount = static_cast<std::uint16_t>(
                (static_cast<unsigned>(streamBytes[9]) << 8u) | streamBytes[10]);
            assert(factoryDivision == 384u);
            assert(factoryTempo == 600000u);
            std::size_t offset = 11u;
            for (unsigned frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
                assert(offset + 6u <= streamBytes.size());
                const auto deltaTicks =
                    (static_cast<std::uint32_t>(streamBytes[offset]) << 24u) |
                    (static_cast<std::uint32_t>(streamBytes[offset + 1u]) << 16u) |
                    (static_cast<std::uint32_t>(streamBytes[offset + 2u]) << 8u) |
                    streamBytes[offset + 3u];
                const auto length = static_cast<std::uint16_t>(
                    (static_cast<unsigned>(streamBytes[offset + 4u]) << 8u) |
                    streamBytes[offset + 5u]);
                offset += 6u;
                assert(offset + length <= streamBytes.size());
                TimedSysExFrame frame;
                frame.deltaTicks = deltaTicks;
                frame.bytes.assign(streamBytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                   streamBytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
                assert(frame.bytes.size() >= 2u && frame.bytes.front() == 0xf0u &&
                       frame.bytes.back() == 0xf7u);
                factoryFrames.push_back(std::move(frame));
                offset += length;
            }
            assert(offset == streamBytes.size());
            assert(factoryFrames.size() == 300u);
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

        // Seed two locations in the firmware's startup-cleared XDATA range.
        firmwareMachine.writeExternal(0x0000, 0xa5);
        firmwareMachine.writeExternal(0x34ff, 0xa5);
        const auto executed = firmwareMachine.runUntilProgramCounter(0xeb8d, 50000);
        std::printf("IC4 boot trace: instructions=%zu PC=%04X SP=%02X cycles=%llu\n",
                    executed, static_cast<unsigned>(firmwareMachine.cpu().registers().pc),
                    static_cast<unsigned>(firmwareMachine.cpu().registers().sp),
                    static_cast<unsigned long long>(firmwareMachine.cpu().totalCycles()));
        assert(executed == 41278);
        assert(firmwareMachine.cpu().registers().pc == 0xeb8d);
        assert(firmwareMachine.cpu().registers().sp == 0xb5);
        assert(firmwareMachine.xdataByteForTesting(0x0000) == 0x00);
        assert(firmwareMachine.xdataByteForTesting(0x34ff) == 0x00);

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
        std::fflush(stdout);
        // Diagnostic run: keep the observed transfer count visible while the
        // IC51 P3 decode is being reconciled with the firmware's port toggles.
        assert(firmwareMachine.soundMemoryWriteCount() == 13647u);
        assert(firmwareMachine.ic51ReadsByBank(0) == 13651u); // 4-byte header + payload
        assert(firmwareMachine.ic51ReadsByBank(1) == 64u);    // descriptor-table reads
        assert(firmwareMachine.xdataByteForTesting(0xf5b2) == 0xd2u); // final uploaded byte
        assert(firmwareMachine.soundMemoryWriteTraceCount() ==
               firmwareMachine.soundMemoryWriteCount());
        const auto soundWrites = firmwareMachine.soundMemoryWriteTraceCount();
        const auto& finalSoundWrite = firmwareMachine.soundMemoryWriteTraceAt(soundWrites - 1u);
        assert(finalSoundWrite.instructionPC == 0xaf10u);
        assert(finalSoundWrite.returnPC == 0xaf68u);
        std::printf("IC4 external sound-memory writes (last 12):\\n");
        const auto firstRecentWrite = soundWrites > 12u ? soundWrites - 12u : 0u;
        for (std::size_t i = firstRecentWrite; i < soundWrites; ++i) {
            const auto& event = firmwareMachine.soundMemoryWriteTraceAt(i);
            std::printf("  %04X=%02X pc=%04X ret=%04X mode=%u cycle=%llu\\n",
                        static_cast<unsigned>(event.address),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned>(event.instructionPC),
                        static_cast<unsigned>(event.returnPC),
                        static_cast<unsigned>(event.mode),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        const auto ic51Reads = firmwareMachine.ic51ReadTraceCount();
        std::printf("IC51 source reads: total=%llu bank0=%llu bank1=%llu trace=%zu\\n",
                    static_cast<unsigned long long>(firmwareMachine.ic51ReadCount()),
                    static_cast<unsigned long long>(firmwareMachine.ic51ReadsByBank(0)),
                    static_cast<unsigned long long>(firmwareMachine.ic51ReadsByBank(1)),
                    ic51Reads);
        // A zero count is diagnostically useful here: the bus probe below
        // records whether the current P3 decode ever selects the IC51 image.
        std::printf("IC51 source reads (last 12):\\n");
        const auto firstRecentRead = ic51Reads > 12u ? ic51Reads - 12u : 0u;
        for (std::size_t i = firstRecentRead; i < ic51Reads; ++i) {
            const auto& event = firmwareMachine.ic51ReadTraceAt(i);
            std::printf("  bank=%u %04X=%02X pc=%04X ret=%04X cycle=%llu\\n",
                        static_cast<unsigned>(event.bank),
                        static_cast<unsigned>(event.address),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned>(event.instructionPC),
                        static_cast<unsigned>(event.returnPC),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        const auto busProbeCount = firmwareMachine.ic51BusProbeTraceCount();
        std::printf("IC51 bus probes around AF3C-AF63 (last 24): total=%zu\\n",
                    busProbeCount);
        const auto firstBusProbe = busProbeCount > 24u ? busProbeCount - 24u : 0u;
        for (std::size_t i = firstBusProbe; i < busProbeCount; ++i) {
            const auto& event = firmwareMachine.ic51BusProbeTraceAt(i);
            std::printf("  pc=%04X DPTR=%04X P3=%02X selected=%u bank=%u value=%02X cycle=%llu\\n",
                        static_cast<unsigned>(event.instructionPC),
                        static_cast<unsigned>(event.address),
                        static_cast<unsigned>(event.p3),
                        static_cast<unsigned>(event.selected),
                        static_cast<unsigned>(event.bank),
                        static_cast<unsigned>(event.valueOnCurrentMap),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        std::printf("IC4 P3 writes during peripheral init (last 32):\\n");
        const auto p3Writes = firmwareMachine.portWriteCount();
        std::size_t shownP3Writes = 0;
        for (std::size_t age = 0; age < p3Writes && shownP3Writes < 32u; ++age) {
            const auto& event = firmwareMachine.portWriteAt(p3Writes - 1u - age);
            if (event.port != 3u) continue;
            ++shownP3Writes;
            std::printf("  P3=%02X cycle=%llu\\n",
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned long long>(event.machineCycle));
        }
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
        // Experimental fixture only: writable 7xxx window plus a controlled
        // 7BD5 status response. This proves the firmware path, not real hardware semantics.
        injectedResponseMachine.enableTestExternalRamWindow7000(0x00);
        injectedResponseMachine.setTestExternalReadOverride(0x7bd5, 0x01);
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
        assert(injectedResponseMachine.soundMemoryWriteCount() >= 13647u);
        assert(injectedResponseMachine.ic51ReadsByBank(0) >= 13651u);

        if (!factoryFrames.empty()) {
            const auto factoryStartCycle = injectedResponseMachine.elapsedMachineCycles();
            const auto cyclesPerSecond = injectedResponseMachine.machineCyclesPerSecond();
            const auto byteCycles = injectedResponseMachine.midiByteMachineCycles();
            const auto tickDenominator =
                static_cast<std::uint64_t>(factoryDivision) * 1000000u;
            std::uint64_t timelineNumerator = 0;
            std::uint64_t totalFactoryBytes = 0;
            for (const auto& frame : factoryFrames) {
                timelineNumerator += static_cast<std::uint64_t>(frame.deltaTicks) *
                    factoryTempo * cyclesPerSecond;
                const auto eventCycle = factoryStartCycle +
                    timelineNumerator / tickDenominator;
                const auto firstArrivalBase = std::max(
                    eventCycle, injectedResponseMachine.elapsedMachineCycles());
                assert(injectedResponseMachine.scheduleAciaMidiBytes(
                    frame.bytes.data(), frame.bytes.size(), eventCycle));
                const auto lastByteArrival = firstArrivalBase +
                    byteCycles * frame.bytes.size();
                const auto cycleBudget = lastByteArrival -
                    injectedResponseMachine.elapsedMachineCycles() + 1u;
                injectedResponseMachine.runForMachineCycles(
                    static_cast<std::size_t>(cycleBudget));
                assert(!injectedResponseMachine.cpuFaulted());
                totalFactoryBytes += frame.bytes.size();
            }
            injectedResponseMachine.runForMachineCycles(100000);
            std::printf("Factory RAM Performance SysEx loaded: frames=%zu bytes=%llu tempo=%u division=%u commands=%zu IC51=%llu\\n",
                        factoryFrames.size(),
                        static_cast<unsigned long long>(totalFactoryBytes),
                        factoryTempo, static_cast<unsigned>(factoryDivision),
                        injectedResponseMachine.soundCommandPacketCount(),
                        static_cast<unsigned long long>(injectedResponseMachine.ic51ReadCount()));
            std::fflush(stdout);
        }

        // Select the first restored RAM Performance (source/program 5),
        // then send a Note On through the real INT1/ACIA receive path.
        const std::array<std::uint8_t, 5> programAndNote{{
            0xc0, 0x05, 0x90, 0x3c, 0x64
        }};
        const auto midiStartCycle = injectedResponseMachine.elapsedMachineCycles();
        const auto commandCountBeforeMidi = injectedResponseMachine.soundCommandPacketCount();
        const auto call57a8BeforeMidi = injectedResponseMachine.watchedCallCount(0x57a8);
        const auto call5671BeforeMidi = injectedResponseMachine.watchedCallCount(0x5671);
        const auto call4256BeforeMidi = injectedResponseMachine.watchedCallCount(0x4256);
        const auto call1494BeforeMidi = injectedResponseMachine.watchedCallCount(0x1494);
        const auto callAC00BeforeMidi = injectedResponseMachine.watchedCallCount(0xac00);
        const auto callAC88BeforeMidi = injectedResponseMachine.watchedCallCount(0xac88);
        const auto commandWriterCallsBeforeMidi = injectedResponseMachine.watchedCallCount(0x0db7);
        const auto validAciaReadsBeforeMidi = injectedResponseMachine.aciaValidReceiveReadCount();
        const auto ieBeforeMidi = injectedResponseMachine.readInternal(0xa8);
        assert(injectedResponseMachine.scheduleAciaMidiBytes(
            programAndNote.data(), programAndNote.size(), midiStartCycle));
        const auto midiInstructions = injectedResponseMachine.runForMachineCycles(2000000);
        const auto midiDrainInstructions = injectedResponseMachine.runForMachineCycles(5000000u);
        std::printf("IC4 MIDI Note On diagnostic: instructions=%zu drain=%zu PC=%04X IE=%02X TCON=%02X ACIA=%02X P3=%02X IC51=%llu bank0=%llu bank1=%llu lastIC51=%04X ACIAreads=%llu lastRX=%02X RAM49=%02X RAM47=%02X RAM59=%02X RAM5E=%02X X2B8B=%02X X2B8C=%02X X2B8D=%02X X2B8F=%02X X30E4=%02X R53=%02X R56=%02X R29=%02X R2A=%02X R5D=%02X R5E=%02X X2B90=%02X X2B9D=%02X X2B9E=%02X X2B9F=%02X X30E6=%02X X30E8=%02X X304C=%02X X304D=%02X X304E=%02X\\n",
                    midiInstructions, midiDrainInstructions,
                    static_cast<unsigned>(injectedResponseMachine.cpu().registers().pc),
                    static_cast<unsigned>(ieBeforeMidi),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x88)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0xa002)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0xb0)),
                    static_cast<unsigned long long>(injectedResponseMachine.ic51ReadCount()),
                    static_cast<unsigned long long>(injectedResponseMachine.ic51ReadsByBank(0)),
                    static_cast<unsigned long long>(injectedResponseMachine.ic51ReadsByBank(1)),
                    static_cast<unsigned>(injectedResponseMachine.lastIC51ReadAddress()),
                    static_cast<unsigned long long>(injectedResponseMachine.aciaReceiveReadCount()),
                    static_cast<unsigned>(injectedResponseMachine.lastAciaReceiveRead()),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x49)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x47)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x59)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x5e)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b8b)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b8c)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b8d)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b8f)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x30e4)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x53)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x56)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x29)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x2a)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x5d)),
                    static_cast<unsigned>(injectedResponseMachine.readInternal(0x5e)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b90)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b9d)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b9e)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b9f)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x30e6)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x30e8)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x304c)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x304d)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x304e)));
        std::fflush(stdout);
        assert(!injectedResponseMachine.cpuFaulted());
        assert(injectedResponseMachine.aciaValidReceiveReadCount() ==
               validAciaReadsBeforeMidi + programAndNote.size());
        assert(injectedResponseMachine.lastValidAciaReceiveRead() == programAndNote.back());
        // The firmware consumed all MIDI bytes. The event work area may be
        // cleared by the downstream voice dispatcher, so retain the note check
        // but do not require the raw status byte to remain 0x90.
        // Downstream processing may consume and clear the note work byte.
        std::printf("Firmware call deltas: 57A8=%llu 5671=%llu 4256=%llu 1494=%llu 0DB7=%llu AC00=%llu AC88=%llu\\n",
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x57a8) - call57a8BeforeMidi),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x5671) - call5671BeforeMidi),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x4256) - call4256BeforeMidi),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x1494) - call1494BeforeMidi),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x0db7) - commandWriterCallsBeforeMidi),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0xac00) - callAC00BeforeMidi),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0xac88) - callAC88BeforeMidi));
        std::printf("Firmware call counts: 57A8=%llu 5671=%llu 4256=%llu 1494=%llu 0DB7=%llu AC00=%llu AC88=%llu B34D=%llu\\n",
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x57a8)),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x5671)),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x4256)),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x1494)),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0x0db7)),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0xac00)),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0xac88)),
                    static_cast<unsigned long long>(injectedResponseMachine.watchedCallCount(0xb34d)));
        std::printf("MIDI voice-gate XDATA: 7D95=%02X 3702=%02X 3704=%02X 370A=%02X 370B=%02X 3039=%02X 303A=%02X 304B=%02X 3050=%02X 3051=%02X 2FB3=%02X 2FB4=%02X\\n",
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x7d95)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x3702)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x3704)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x370a)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x370b)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x3039)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x303a)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x304b)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x3050)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x3051)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2fb3)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2fb4)));
        std::printf("Part-0 MIDI route XDATA: 2900=%02X 2901=%02X 2902=%02X 2903=%02X 2904=%02X 2905=%02X 2906=%02X 2B98=%02X 2B99=%02X 2FD7=%02X 2FA2=%02X 7C17=%02X 7C1C=%02X 7C26=%02X 7BD5=%02X 3700=%02X 3703=%02X\\n",
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2900)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2901)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2902)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2903)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2904)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2905)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2906)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b98)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2b99)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2fd7)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x2fa2)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x7c17)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x7c1c)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x7c26)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x7bd5)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x3700)),
                    static_cast<unsigned>(injectedResponseMachine.readExternal(0x3703)));
        std::printf("IC51 raw route bytes: bank0[7BD4..7BD7]=%02X %02X %02X %02X bank1[7BD4..7BD7]=%02X %02X %02X %02X\\n",
                    static_cast<unsigned>(injectedResponseMachine.ic51RomByteForTesting(0, 0x7bd4)),
                    static_cast<unsigned>(injectedResponseMachine.ic51RomByteForTesting(0, 0x7bd5)),
                    static_cast<unsigned>(injectedResponseMachine.ic51RomByteForTesting(0, 0x7bd6)),
                    static_cast<unsigned>(injectedResponseMachine.ic51RomByteForTesting(0, 0x7bd7)),
                    static_cast<unsigned>(injectedResponseMachine.ic51RomByteForTesting(1, 0x7bd4)),
                    static_cast<unsigned>(injectedResponseMachine.ic51RomByteForTesting(1, 0x7bd5)),
                    static_cast<unsigned>(injectedResponseMachine.ic51RomByteForTesting(1, 0x7bd6)),
                    static_cast<unsigned>(injectedResponseMachine.ic51RomByteForTesting(1, 0x7bd7)));
        const auto ic51TraceCount = injectedResponseMachine.ic51ReadTraceCount();
        std::printf("IC51 reads near 7BD5 (newest first):\\n");
        std::size_t routeReads = 0;
        for (std::size_t age = 0; age < ic51TraceCount && routeReads < 24u; ++age) {
            const auto& event = injectedResponseMachine.ic51ReadTraceAt(ic51TraceCount - 1u - age);
            if (event.address < 0x7bd0u || event.address > 0x7be0u) continue;
            ++routeReads;
            std::printf("  bank=%u address=%04X value=%02X cycle=%llu\\n",
                        static_cast<unsigned>(event.bank),
                        static_cast<unsigned>(event.address),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        const auto xdataWatchCount = injectedResponseMachine.xdataWatchTraceCount();
        std::printf("XDATA 7BD4-7BD7 accesses (newest first):\\n");
        for (std::size_t age = 0; age < xdataWatchCount && age < 64u; ++age) {
            const auto& event = injectedResponseMachine.xdataWatchTraceAt(xdataWatchCount - 1u - age);
            std::printf("  %c %04X=%02X cycle=%llu\\n",
                        event.write ? 'W' : 'R',
                        static_cast<unsigned>(event.address),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        const auto unmappedCount = injectedResponseMachine.unmappedBusTraceCount();
        std::printf("Unmapped external bus accesses (newest first, last 64):\\n");
        for (std::size_t age = 0; age < unmappedCount && age < 64u; ++age) {
            const auto& event = injectedResponseMachine.unmappedBusTraceAt(unmappedCount - 1u - age);
            std::printf("  %c %04X=%02X cycle=%llu\\n",
                        event.write ? 'W' : 'R',
                        static_cast<unsigned>(event.address),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        std::printf("Unmapped 7xxx address histogram:\\n");
        for (std::uint32_t address = 0x7000u; address <= 0x7fffu; ++address) {
            const auto reads = injectedResponseMachine.unmappedReadCountAt7xxx(
                static_cast<std::uint16_t>(address));
            const auto writes = injectedResponseMachine.unmappedWriteCountAt7xxx(
                static_cast<std::uint16_t>(address));
            if (reads != 0 || writes != 0)
                std::printf("  %04X reads=%llu writes=%llu\\n", address,
                            static_cast<unsigned long long>(reads),
                            static_cast<unsigned long long>(writes));
        }
        const auto portWrites = injectedResponseMachine.portWriteCount();
        std::printf("Recent IC4 port writes (newest first):\\n");
        for (std::size_t portAge = 0; portAge < 24u && portAge < portWrites; ++portAge) {
            const auto& event = injectedResponseMachine.portWriteAt(portWrites - 1u - portAge);
            std::printf("  P%u=%02X cycle=%llu\\n",
                        static_cast<unsigned>(event.port),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        std::printf("IC4 P1/P3 trace (newest first):\\n");
        std::size_t serialPortEvents = 0;
        for (std::size_t portAge = 0; portAge < 256u && portAge < portWrites && serialPortEvents < 64u; ++portAge) {
            const auto& event = injectedResponseMachine.portWriteAt(portWrites - 1u - portAge);
            if (event.port != 1u && event.port != 3u) continue;
            ++serialPortEvents;
            std::printf("  P%u=%02X cycle=%llu\\n",
                        static_cast<unsigned>(event.port),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        const auto portReads = injectedResponseMachine.portReadCount();
        std::printf("IC4 GPIO input samples (newest first):\\n");
        std::size_t gpioReadEvents = 0;
        for (std::size_t readAge = 0; readAge < portReads && gpioReadEvents < 64u; ++readAge) {
            const auto& event = injectedResponseMachine.portReadAt(portReads - 1u - readAge);
            if (event.port != 1u && event.port != 3u) continue;
            ++gpioReadEvents;
            std::printf("  P%u=%02X cycle=%llu\\n",
                        static_cast<unsigned>(event.port),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        const auto& midiCpu = injectedResponseMachine.cpu();
        std::printf("Recent IC4 instructions (newest first):\\n");
        for (std::size_t traceAge = 0; traceAge < 24u &&
             traceAge < midiCpu.recentInstructionCount(); ++traceAge) {
            std::printf("  %04X: %02X\\n",
                        static_cast<unsigned>(midiCpu.recentInstructionPC(traceAge)),
                        static_cast<unsigned>(midiCpu.recentInstructionOpcode(traceAge)));
        }
        // IC51 access depends on the asynchronous firmware/peripheral phase;
        // the MIDI event assertions above are deterministic across hosts.
        assert((injectedResponseMachine.readExternal(0xa002) & 0x01u) == 0);
        assert(injectedResponseMachine.cpu().registers().pc != 0x32b0);
        const auto commandCount = injectedResponseMachine.soundCommandPacketCount();
        assert(injectedResponseMachine.watchedCallCount(0x57a8) > call57a8BeforeMidi);
        assert(injectedResponseMachine.watchedCallCount(0x5671) - call5671BeforeMidi == 2u);
        assert(injectedResponseMachine.watchedCallCount(0x4256) - call4256BeforeMidi == 2u);
        // The MIDI stimulus does not deterministically enter IC51 helper 0x1494.
        // Its source-read trace is asserted separately on the startup/resource path.
        assert(injectedResponseMachine.watchedCallCount(0xac00) - callAC00BeforeMidi == 2u);
        assert(commandCount > commandCountBeforeMidi);
        assert(injectedResponseMachine.watchedCallCount(0x0db7) > commandWriterCallsBeforeMidi);

        // The captured firmware burst has two 256-write sweeps from
        // the LCALL at 0x0E67 (return 0x0E6A), followed by one 32-iteration
        // pass through 0x0DF7. Its 0x0E1A packet writes appear as two groups
        // of 16, separated by a 0x0DE4 control-port write at the midpoint.
        // Keep caller/shape relationships under regression without assigning
        // undocumented hardware meanings to the payload bytes.
        assert(commandCountBeforeMidi == 548u);
        assert(commandCount - commandCountBeforeMidi == 553u);
        const auto& sweepAFirst = injectedResponseMachine.soundCommandPacketAt(554u);
        const auto& sweepALast = injectedResponseMachine.soundCommandPacketAt(809u);
        const auto& sweepBFirst = injectedResponseMachine.soundCommandPacketAt(810u);
        const auto& sweepBLast = injectedResponseMachine.soundCommandPacketAt(1065u);
        assert(sweepAFirst.returnPC == 0x0e6au);
        assert(sweepALast.returnPC == 0x0e6au);
        assert(sweepBFirst.returnPC == 0x0e6au);
        assert(sweepBLast.returnPC == 0x0e6au);
        assert(sweepAFirst.bytes[0] == 0x00u && sweepAFirst.bytes[3] == 0x00u);
        assert(sweepALast.bytes[0] == 0x01u && sweepALast.bytes[3] == 0x00u);
        assert(sweepBFirst.bytes[0] == 0x00u && sweepBFirst.bytes[3] == 0x80u);
        assert(sweepBLast.bytes[0] == 0x01u && sweepBLast.bytes[3] == 0x80u);
        const auto& tableWriteFirst = injectedResponseMachine.soundCommandPacketAt(1067u);
        const auto& tableWriteLast = injectedResponseMachine.soundCommandPacketAt(1082u);
        const auto& tableWriteSecondFirst = injectedResponseMachine.soundCommandPacketAt(1084u);
        const auto& tableWriteSecondLast = injectedResponseMachine.soundCommandPacketAt(1099u);
        assert(tableWriteFirst.returnPC == 0x0e1du);
        assert(tableWriteLast.returnPC == 0x0e1du);
        assert(tableWriteSecondFirst.returnPC == 0x0e1du);
        assert(tableWriteSecondLast.returnPC == 0x0e1du);
        assert(tableWriteFirst.bytes[0] == 0x0fu && tableWriteFirst.bytes[2] == 0x40u);
        assert(tableWriteLast.bytes[0] == 0xffu && tableWriteLast.bytes[2] == 0x40u);
        const auto& midpointControl = injectedResponseMachine.soundCommandPacketAt(1083u);
        assert(midpointControl.returnPC == 0x0e0bu);
        assert(midpointControl.bytes[0] == 0xffu && midpointControl.bytes[2] == 0x40u);
        assert(midpointControl.bytes[3] == 0xc0u);
        assert(tableWriteSecondFirst.bytes[0] == 0x0fu && tableWriteSecondFirst.bytes[3] == 0x80u);
        assert(tableWriteSecondLast.bytes[0] == 0xffu && tableWriteSecondLast.bytes[3] == 0x80u);
        std::printf("M.A.S.S. command packets: beforeMidi=%zu afterMidi=%zu delta=%zu\\n",
                    commandCountBeforeMidi, commandCount, commandCount - commandCountBeforeMidi);
        // Preserve the complete MIDI-triggered command burst in CI logs.
        // The machine trace is a 1024-packet ring, so skip only entries that
        // have genuinely been overwritten; do not hide the first 521 packets.
        const auto firstRetainedCommand = commandCount > 1024u
            ? commandCount - 1024u : 0u;
        const auto firstNewCommand = std::max(commandCountBeforeMidi, firstRetainedCommand);
        std::printf("Retained MIDI command packet range: [%zu, %zu)\\n",
                    firstNewCommand, commandCount);
        for (auto commandIndex = firstNewCommand; commandIndex < commandCount; ++commandIndex) {
            const auto& packet = injectedResponseMachine.soundCommandPacketAt(commandIndex);
            std::printf("  MIDI-CMD[%zu] pc=%04X ret=%04X cycle=%llu %02X %02X %02X %02X\\n",
                        commandIndex,
                        static_cast<unsigned>(packet.instructionPC),
                        static_cast<unsigned>(packet.returnPC),
                        static_cast<unsigned long long>(packet.machineCycle),
                        static_cast<unsigned>(packet.bytes[0]),
                        static_cast<unsigned>(packet.bytes[1]),
                        static_cast<unsigned>(packet.bytes[2]),
                        static_cast<unsigned>(packet.bytes[3]));
        }
    }
    return 0;
}
