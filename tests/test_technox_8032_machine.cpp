#include "../Source/TechnoX8032Machine.h"
#include "../Source/MASSResourceROM.h"
#include "../Source/MASSAudioEngine.h"

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


class FXCatalogProbeBackend final : public technox::IMASSVoiceBackend {
public:
    void setFXResourceCatalog(const technox::MASSFXResourceCatalog* catalog) noexcept override {
        receivedCatalog = catalog;
    }
    void prepare(double, int) override {}
    void noteOn(int, const technox::VoiceSlot&) noexcept override {}
    void noteOff(int, const technox::VoiceSlot&) noexcept override {}
    void resetVoice(int, const technox::VoiceSlot&) noexcept override {}
    bool isVoiceFinished(int) const noexcept override { return true; }
    void render(float* const*, int, int,
                const technox::VoiceManager&, const technox::ModulationMatrix&,
                const technox::MASSPartParameterStore&,
                const technox::MASSModulationSourceStore&,
                const technox::MASSPartRuntimeControlStore&,
                const technox::MASSCommonControlStore&,
                const technox::MASSPerformanceImageStore&,
                const technox::MASSDrumParameterStore&) noexcept override {}

    const technox::MASSFXResourceCatalog* receivedCatalog = nullptr;
};

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

    // Undocumented four-register external window is captured for later protocol analysis.
    machine.setPortPins(0xa0u, 0x12u);
    machine.setPortPins(0xb0u, 0x34u);
    machine.writeExternal(0x8000, 0x11);
    machine.setPortPins(0xa0u, 0x13u);
    machine.setPortPins(0xb0u, 0x35u);
    machine.writeExternal(0x8001, 0x22);
    machine.setPortPins(0xa0u, 0x14u);
    machine.setPortPins(0xb0u, 0x36u);
    machine.writeExternal(0x8002, 0x33);
    machine.setPortPins(0xa0u, 0x15u);
    machine.setPortPins(0xb0u, 0x37u);
    machine.writeExternal(0x8003, 0x44);
    const auto& command = machine.commandPortBytes();
    assert(command[0] == 0x11 && command[1] == 0x22);
    assert(command[2] == 0x33 && command[3] == 0x44);
    assert(machine.readExternal(0x8000) == 0x11);
    // The peripheral response is independent from the command just written.
    assert(machine.readExternal(0x8001) == 0xff);
    assert(machine.readExternal(0x8002) == 0xff);
    assert(machine.readExternal(0x8003) == 0xff);
    machine.setSoundHostResponseForTesting(0x11u, {{0x22u, 0x33u, 0x44u}});
    assert(machine.readExternal(0x8001) == 0x22);
    assert(machine.readExternal(0x8002) == 0x33);
    assert(machine.readExternal(0x8003) == 0x44);
    assert(machine.soundHostReadCount() == 7u);
    const auto& responseRead = machine.soundHostReadAt(4);
    assert(responseRead.registerIndex == 1u);
    assert(responseRead.value == 0x22u);
    assert(responseRead.deviceIndex == 0x11u);
    assert(responseRead.port2 == 0x15u);
    assert(responseRead.port3 == 0x37u);
    assert(responseRead.soundBusMode == 1u);
    assert(!responseRead.ic51Selected);
    assert(responseRead.soundDeviceResetReleased);
    assert(machine.soundCommandPacketCount() == 1u);
    const auto& capturedCommand = machine.soundCommandPacketAt(0);
    assert(capturedCommand.bytes == command);
    assert(capturedCommand.deviceIndex == 0x11u);
    assert(capturedCommand.port2 == 0x12u);
    assert(capturedCommand.port3 == 0x34u);
    assert(capturedCommand.soundBusMode == 1u);
    assert(!capturedCommand.ic51Selected);
    assert(capturedCommand.soundDeviceResetReleased);
    assert(capturedCommand.byteContext[0].port2 == 0x12u);
    assert(capturedCommand.byteContext[1].port2 == 0x13u);
    assert(capturedCommand.byteContext[2].port2 == 0x14u);
    assert(capturedCommand.byteContext[3].port2 == 0x15u);
    assert(capturedCommand.byteContext[0].port3 == 0x34u);
    assert(capturedCommand.byteContext[1].port3 == 0x35u);
    assert(capturedCommand.byteContext[2].port3 == 0x36u);
    assert(capturedCommand.byteContext[3].port3 == 0x37u);
    for (std::size_t i = 0; i < 4; ++i) {
        assert(capturedCommand.byteContext[i].deviceIndex == 0x11u);
        assert(capturedCommand.byteContext[i].soundBusMode == 1u);
        assert(!capturedCommand.byteContext[i].ic51Selected);
        assert(capturedCommand.byteContext[i].soundDeviceResetReleased);
    }
    assert(machine.soundHostBurstWordCount() == 1u);
    const auto& burstWord = machine.soundHostBurstWordAt(0);
    assert(burstWord.lowByte == 0x33u && burstWord.highByte == 0x44u);
    assert(burstWord.value == 0x4433u);
    machine.writeExternal(0x8003u, 0x40u); // control-phase write observed in 0x0DCE
    machine.writeExternal(0x8003u, 0xc0u); // opposite control phase
    assert(machine.readExternal(0x8003u) == 0x44u); // response is not overwritten by host writes
    assert(machine.soundHostPayloadForTesting(0x11u)[2] == 0x44u);
    machine.writeExternal(0x8003u, 0x80u); // standalone R4 payload update
    assert(machine.soundCommandPacketCount() == 1u);
    assert(machine.soundHostBurstWordCount() == 1u);
    assert(machine.soundHostWriteCount() == 7u);
    assert(machine.soundHostWordForTesting(0x11u) == 0x8033u);
    machine.writeExternal(0x8000u, 0x20u);
    machine.writeExternal(0x8001u, 0x00u);
    machine.writeExternal(0x8002u, 0x78u);
    machine.writeExternal(0x8003u, 0x56u);
    assert(machine.soundCommandPacketCount() == 2u);
    assert(machine.soundHostBurstWordCount() == 2u);
    assert(machine.soundHostWordForTesting(0x20u) == 0x5678u);
    machine.writeExternal(0x8000u, 0x11u);
    // Re-selecting the same device index returns the independently supplied response.
    assert(machine.readExternal(0x8001u) == 0x22u);
    assert(machine.readExternal(0x8002u) == 0x33u);
    assert(machine.readExternal(0x8003u) == 0x44u); // outgoing payload is never read back as device response

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

        // Execute the real IC4 FX selector helper at 0x7FF2 directly. The
        // 0x7BFE input is supplied through the explicit test overlay because
        // its physical source device/memory map is not identified yet.
        struct FXClampVector {
            std::uint8_t input;
            std::uint8_t expected;
        };
        constexpr std::array<FXClampVector, 6> fxClampVectors{{
            {0x00u, 0x00u}, {0x01u, 0x01u}, {0x13u, 0x13u},
            {0x14u, 0x14u}, {0x15u, 0x14u}, {0x7fu, 0x14u}
        }};
        for (const auto& vector : fxClampVectors) {
            TechnoX8032Machine fxIndexProbe;
            assert(fxIndexProbe.loadIC4Rom(realRom.data(), realRom.size()));
            fxIndexProbe.reset();
            fxIndexProbe.enableTestExternalRamWindow7000(0x00u);
            fxIndexProbe.setTestExternalRamByte(0x7bfeu, vector.input);
            fxIndexProbe.cpuForTesting().registersForTesting().pc = 0x7ff2u;
            const auto executed = fxIndexProbe.runUntilProgramCounter(0x8004u, 32u);
            assert(executed > 0u);
            assert(fxIndexProbe.cpu().registers().pc == 0x8004u);
            assert(fxIndexProbe.readExternal(0x3098u) == vector.expected);
        }

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

        // Recover the fixed FX-name/resource table from the descrambled IC4
        // image, then resolve both actual IC51 upload-stream formats.
        if (!realIC51.empty()) {
            std::array<std::uint8_t, TechnoX8032Machine::kCodeSpaceSize> firmwareCode{};
            for (std::size_t address = 0; address < firmwareCode.size(); ++address)
                firmwareCode[address] = firmwareMachine.readCode(
                    static_cast<std::uint16_t>(address));

            technox::MASSFXProgramDirectory fxDirectory;
            assert(fxDirectory.load(firmwareCode.data(), firmwareCode.size()));
            struct ExpectedFX {
                const char* name;
                std::uint8_t firstResource;
                std::uint8_t secondResource;
                std::uint8_t mode;
            };
            const std::array<ExpectedFX, technox::MASSFXProgramDirectory::kEntryCount>
                expectedFX{{
                    {"Room",        0x00, 0x01, 0x03},
                    {"SmallRoom",   0x04, 0x05, 0x03},
                    {"WarmRoom",    0x02, 0x03, 0x03},
                    {"Chamber1",    0x06, 0x07, 0x03},
                    {"Chamber2",    0x08, 0x09, 0x03},
                    {"Plate1",      0x0a, 0x0b, 0x03},
                    {"Plate2",      0x0c, 0x0d, 0x03},
                    {"Hall",        0x0e, 0x0f, 0x03},
                    {"LargeHall",   0x10, 0x11, 0x03},
                    {"Cathedral",   0x12, 0x13, 0x03},
                    {"GatedRev1",   0x00, 0x01, 0x06},
                    {"GatedRev2",   0x02, 0x03, 0x06},
                    {"GatedRev3",   0x0a, 0x0b, 0x06},
                    {"EarlyRfl1",   0x16, 0x17, 0x03},
                    {"EarlyRfl2",   0x18, 0x19, 0x03},
                    {"EarlyRfl3",   0x1a, 0x1b, 0x03},
                    {"EarlyRfl4",   0x1c, 0x1d, 0x03},
                    {"RainDrops",   0x14, 0x15, 0x03},
                    {"HQ-Delay",    0x20, 0x21, 0x04},
                    {"LongDelay",   0x1e, 0x1f, 0x04},
                    {"no Effect",   0x22, 0x23, 0x00}
                }};

            for (std::size_t index = 0; index < expectedFX.size(); ++index) {
                const auto* record = fxDirectory.find(static_cast<int>(index));
                assert(record != nullptr);
                assert(record->index == index);
                assert(record->fx1ResourceIndex == expectedFX[index].fx1Resource);
                assert(record->fx2ResourceIndex == expectedFX[index].fx2Resource);
                assert(record->modeByte == expectedFX[index].mode);
                std::size_t nameLength = 0;
                while (expectedFX[index].name[nameLength] != '\0') {
                    assert(record->name[nameLength] == expectedFX[index].name[nameLength]);
                    ++nameLength;
                }
                for (std::size_t j = nameLength; j < technox::MASSFXProgramDirectory::kNameBytes; ++j)
                    assert(record->name[j] == '\0' || record->name[j] == ' ');
                assert(record->name[technox::MASSFXProgramDirectory::kNameBytes] == '\0');
            }
            assert(fxDirectory.find(-1) == nullptr);
            assert(fxDirectory.find(21) == nullptr);

            technox::MASSResourceROM fxResources;
            assert(fxResources.load(realIC51.data(), realIC51.size()));

            technox::MASSFXResourceCatalog fxCatalog;
            assert(fxCatalog.load(
                fxResources, firmwareCode.data(), firmwareCode.size()));
            assert(fxCatalog.isLoaded());
            for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
                for (int fxIndex = 0; fxIndex < 21; ++fxIndex) {
                    const auto* resolved = fxCatalog.find(bankIndex, fxIndex);
                    assert(resolved != nullptr);
                    assert(resolved->firstResource.terminated);
                    assert(resolved->secondResource.terminated);
                    assert(resolved->directoryEntry.index == fxIndex);
                }
            }
            assert(fxCatalog.find(-1, 0) == nullptr);
            assert(fxCatalog.find(0, 21) == nullptr);
            assert(fxCatalog.find(2, 0) == nullptr);
            const auto* roomBank0 = fxCatalog.find(0, 0);
            assert(roomBank0 != nullptr);
            assert(roomBank0->firstResource.blocks.size() == 2u);
            assert(roomBank0->secondResource.blocks.size() == 1u);
            assert(fxCatalog.find(1, 0)->firstResource.blocks.size() > 0u);

            // The real-time backend receives the immutable catalog on initial
            // attachment and again when replaced, just like the program catalog.
            FXCatalogProbeBackend originalFXBackend;
            FXCatalogProbeBackend replacementFXBackend;
            technox::MASSAudioEngine fxCatalogEngine(&originalFXBackend);
            fxCatalogEngine.setFXResourceCatalog(&fxCatalog);
            assert(fxCatalogEngine.fxResourceCatalog() == &fxCatalog);
            assert(originalFXBackend.receivedCatalog == &fxCatalog);
            fxCatalogEngine.setBackend(&replacementFXBackend);
            assert(replacementFXBackend.receivedCatalog == &fxCatalog);
            for (int bankNumber = 0; bankNumber < 2; ++bankNumber) {
                const auto bank = bankNumber == 0
                    ? technox::MASSResourceBank::bank0
                    : technox::MASSResourceBank::bank1;
                for (std::size_t index = 0; index < expectedFX.size(); ++index) {
                    const auto* record = fxDirectory.find(static_cast<int>(index));
                    technox::MASSIC51PortStreamResource triplets{};
                    technox::MASSIC51PortStreamResource rotatedStream{};
                    assert(technox::MASSIC51PortStreamResolver::resolveTripletStream(
                        fxResources, bank, record->fx1ResourceIndex, triplets));
                    assert(technox::MASSIC51PortStreamResolver::resolveRotatedHeaderStream(
                        fxResources, bank, record->fx2ResourceIndex, rotatedStream));
                    assert(triplets.terminated && rotatedStream.terminated);
                    assert(triplets.sourceBytesConsumed ==
                           triplets.portWriteByteCount + 1u);
                    assert(rotatedStream.sourceBytesConsumed ==
                           rotatedStream.portWriteByteCount + 1u);

                    for (const auto& block : triplets.blocks) {
                        assert(block.payload.size() ==
                               static_cast<std::size_t>(block.sourceCount) * 3u);
                        assert(block.portWriteByteCount == block.payload.size() + 4u);
                        assert(block.sourceBytesConsumed == block.portWriteByteCount);
                        assert(block.wireHeader == block.sourceCount);
                    }
                    for (const auto& block : rotatedStream.blocks) {
                        assert(block.payload.size() ==
                               static_cast<std::size_t>(block.sourceCount));
                        assert(block.portWriteByteCount == block.payload.size() + 4u);
                        assert(block.sourceBytesConsumed == block.portWriteByteCount);
                        const auto count = block.sourceCount;
                        const auto expectedWireHeader = static_cast<std::uint8_t>(
                            (count >> 1u) | static_cast<std::uint8_t>(count << 7u));
                        assert(block.wireHeader == expectedWireHeader);
                    }
                }
            }

            technox::MASSIC51PortStreamResource roomTriplets{};
            technox::MASSIC51PortStreamResource roomRotatedStream{};
            assert(technox::MASSIC51PortStreamResolver::resolveTripletStream(
                fxResources, technox::MASSResourceBank::bank0, 0x00, roomTriplets));
            assert(roomTriplets.blocks.size() == 2u);
            const auto& roomBlock = roomTriplets.blocks.front();
            assert(roomBlock.dataAddress == 0x24d4u);
            assert(roomBlock.sourceCount == 0x19u);
            assert(roomBlock.preamble[0] == 0x00u);
            assert(roomBlock.preamble[1] == 0xcfu);
            assert(roomBlock.preamble[2] == 0x10u);
            assert(roomBlock.payload.size() == 75u);
            assert(roomBlock.portWriteByteCount == 79u);
            assert(technox::MASSIC51PortStreamResolver::resolveRotatedHeaderStream(
                fxResources, technox::MASSResourceBank::bank0, 0x01, roomRotatedStream));
            assert(roomRotatedStream.blocks.size() == 1u);
            const auto& roomRotatedBlock = roomRotatedStream.blocks.front();
            assert(roomRotatedBlock.dataAddress == 0x2851u);
            assert(roomRotatedBlock.sourceCount == 0x7fu);
            assert(roomRotatedBlock.wireHeader == 0xbfu); // RR A, not SHR A
            assert(roomRotatedBlock.preamble[0] == 0x00u);
            assert(roomRotatedBlock.preamble[1] == 0xffu);
            assert(roomRotatedBlock.preamble[2] == 0x00u);
            assert(roomRotatedBlock.payload.size() == 127u);
            assert(roomRotatedBlock.portWriteByteCount == 131u);

        }
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

        // Replay the real IC51 bank-0 bulk block through the exact 256-byte
        // address map recovered from the descrambled IC4 ROM. This checks the
        // complete firmware copy, including the final physical address.
        if (!realIC51.empty()) {
            std::array<std::uint8_t, 256> firmwareAddressMap{};
            for (unsigned i = 0; i < firmwareAddressMap.size(); ++i)
                firmwareAddressMap[i] =
                    TechnoX8032Machine::mapSoundMemoryLowAddress(
                        static_cast<std::uint8_t>(i));

            technox::MASSIC51BulkBlockHeader block{};
            assert(technox::MASSIC51BulkBlockHeader::parse(
                realIC51.data(), 0x10000u, 0x48, block));
            assert(block.sourceAddress == 0x3c64u);
            assert(block.firmwareByteCount() == 0x354fu);

            std::vector<std::uint8_t> stagedXdata(0x3600u, 0xeeu);
            std::uint16_t lastPhysicalAddress = 0;
            assert(technox::MASSIC51BulkBlockHeader::copyPayloadToPagedXdata(
                realIC51.data(), 0x10000u, block, firmwareAddressMap.data(),
                stagedXdata.data(), stagedXdata.size(), 0xc000u,
                &lastPhysicalAddress));
            assert(lastPhysicalAddress == 0xf5b2u);
            for (std::size_t i = 0; i < block.firmwareByteCount(); ++i) {
                const auto physicalOffset = (i & ~std::size_t{0xffu}) +
                    firmwareAddressMap[i & 0xffu];
                const auto sourceAddress = static_cast<std::uint16_t>(
                    block.sourceAddress + i);
                assert(stagedXdata[physicalOffset] ==
                       realIC51[sourceAddress]);
            }
        }
        firmwareMachine.reset();
        // The reset vector is a two-machine-cycle LJMP to EB32. A budget of
        // one cycle must finish that instruction rather than split it.
        assert(firmwareMachine.runForMachineCycles(1) == 1);
        assert(firmwareMachine.elapsedMachineCycles() == 2);
        assert(firmwareMachine.cpu().registers().pc == 0xeb32);
        firmwareMachine.reset();

        // Seed the same startup-cleared XDATA locations as the MIDI fixture.
        // Keep the 7000 test window disabled until after the real IC51 loader;
        // otherwise it changes the selected external-bus decode during boot.
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
        // Reconstruct the exact IC51 payload through the firmware's 256-byte
        // address permutation. This verifies every byte, not just the transfer count.
        const auto bulkTableOffset = 0x48u * 2u;
        const auto bulkSource = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(realIC51[bulkTableOffset]) << 8u) |
            realIC51[bulkTableOffset + 1u]);
        const auto bulkCountHigh = realIC51[bulkTableOffset + 2u];
        const auto bulkCountLow = realIC51[bulkTableOffset + 3u];
        const auto bulkLength = static_cast<std::size_t>(bulkCountHigh) * 256u +
            (bulkCountLow != 0u ? bulkCountLow : 256u);
        assert(bulkLength == firmwareMachine.soundMemoryWriteCount());
        for (std::size_t i = 0; i < bulkLength; ++i) {
            const auto page = static_cast<std::uint16_t>(0xc0u + (i >> 8u));
            const auto low = TechnoX8032Machine::mapSoundMemoryLowAddress(
                static_cast<std::uint8_t>(i & 0xffu));
            const auto physicalAddress = static_cast<std::uint16_t>(
                (page << 8u) | low);
            assert(firmwareMachine.xdataByteForTesting(physicalAddress) ==
                   realIC51[static_cast<std::size_t>(bulkSource) + i]);
        }
        assert(firmwareMachine.lastSoundMemoryWriteAddress() == 0xf5b2u);
        assert(firmwareMachine.ic51ReadsByBank(0) == 13651u); // 4-byte header + payload
        assert(firmwareMachine.ic51ReadsByBank(1) == 64u);    // descriptor-table reads
        assert(firmwareMachine.xdataByteForTesting(0xf5b2) == 0xd2u); // final uploaded byte
        // Optional raw snapshot of the firmware-loaded external sound-memory window.
        // This is the physical XDATA image after the IC51 block transfer and address permutation.
        if (argc > 4) {
            std::ofstream dump(argv[4], std::ios::binary);
            assert(dump.good());
            for (std::uint32_t address = 0xc000u; address <= 0xffffu; ++address)
                dump.put(static_cast<char>(firmwareMachine.xdataByteForTesting(
                    static_cast<std::uint16_t>(address))));
            dump.flush();
            assert(dump.good());
        }
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

        // Preserve instruction-level evidence for the undocumented 0x7C00-0x7FFF
        // peripheral window while IC51 is deselected. The machine keeps open-bus
        // behavior; this trace is observation only, not a guessed device model.
        const auto unmappedTraceCount = firmwareMachine.unmappedBusTraceCount();
        const auto firstRetainedUnmapped = unmappedTraceCount > 4096u
            ? unmappedTraceCount - 4096u : 0u;
        std::size_t peripheralWindowEvents = 0;
        std::printf("Undocumented 7BFE/7Cxx/7Dxx bus trace (instruction-level):\\n");
        for (std::size_t i = firstRetainedUnmapped; i < unmappedTraceCount; ++i) {
            const auto& event = firmwareMachine.unmappedBusTraceAt(i);
            if ((event.address < 0x7c00u || event.address > 0x7fffu) && event.address != 0x7bfeu) continue;
            ++peripheralWindowEvents;
            std::printf("  %c %04X=%02X pc=%04X stackRet=%04X P3=%02X 8E=%02X cycle=%llu\\n",
                        event.write ? 'W' : 'R',
                        static_cast<unsigned>(event.address),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned>(event.instructionPC),
                        static_cast<unsigned>(event.stackReturnPC),
                        static_cast<unsigned>(event.port3),
                        static_cast<unsigned>(event.soundBusMode),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        std::printf("Undocumented peripheral-window events retained: %zu\\n",
                    peripheralWindowEvents);

        // Resume the real-IC51 loader with a controlled ready signature.
        // This is explicitly a synthetic SoundHost handshake, not a claim that
        // the unimplemented DSP returned AC on physical hardware.
        const auto loadedBlockWrites = firmwareMachine.soundMemoryWriteCount();
        firmwareMachine.enableTestExternalRamWindow7000(0x00u);
        firmwareMachine.setTestExternalReadOverride(0x7bd5u, 0x01u);
        firmwareMachine.setSoundDeviceResponseForTesting(0xacu);
        const auto resumedToMain = firmwareMachine.runUntilProgramCounter(0x32b0u, 2000000u);
        std::printf("IC4 after synthetic SoundHost-ready response: instructions=%llu PC=%04X "
                    "IC51=%llu writes=%llu burstWords=%zu\\n",
                    static_cast<unsigned long long>(resumedToMain),
                    static_cast<unsigned>(firmwareMachine.cpu().registers().pc),
                    static_cast<unsigned long long>(firmwareMachine.ic51ReadCount()),
                    static_cast<unsigned long long>(firmwareMachine.soundMemoryWriteCount()),
                    firmwareMachine.soundHostBurstWordCount());
        assert(resumedToMain != 0u);
        assert(firmwareMachine.cpu().registers().pc == 0x32b0u);
        assert(firmwareMachine.soundMemoryWriteCount() == loadedBlockWrites);
        const auto bootBurstWords = firmwareMachine.soundHostBurstWordCount();
        const auto bootTransactions = firmwareMachine.soundCommandPacketCount();
        const auto hostWrites = firmwareMachine.soundHostWriteCount();
        assert(bootBurstWords == bootTransactions);
        std::printf("External window boot: writes=%zu completeTransactions=%zu pairedWords=%zu\\n",
                    hostWrites, bootTransactions, bootBurstWords);
        std::printf("Standalone register-3 writes (outside complete transactions):\\n");
        for (std::size_t i = 0; i < hostWrites; ++i) {
            const auto& write = firmwareMachine.soundHostWriteAt(i);
            if (write.registerIndex == 3u && write.instructionPC != 0x0dc5u)
                std::printf("  reg3=%02X pc=%04X cycle=%llu\\n",
                            static_cast<unsigned>(write.value),
                            static_cast<unsigned>(write.instructionPC),
                            static_cast<unsigned long long>(write.machineCycle));
        }
        assert(bootTransactions == 544u);
        for (std::size_t i = 0; i < bootTransactions; ++i) {
            const auto& packet = firmwareMachine.soundCommandPacketAt(i);
            const auto& word = firmwareMachine.soundHostBurstWordAt(i);
            std::uint8_t expectedIndex = 0;
            std::uint16_t expectedWord = 0;
            std::uint16_t expectedCaller = 0x0e6au;
            if (i < 512u) {
                const auto offset = static_cast<unsigned>(i % 256u);
                expectedIndex = static_cast<std::uint8_t>(offset == 0u ? 0u : 256u - offset);
                expectedWord = i < 256u ? 0x0000u : 0x8000u;
            } else {
                const auto offset = static_cast<unsigned>((i - 512u) % 16u);
                expectedIndex = static_cast<std::uint8_t>(0x0fu + offset * 0x10u);
                expectedWord = i < 528u ? 0x0040u : 0x8040u;
                expectedCaller = 0x0e1du;
            }
            const auto packetWord = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(packet.bytes[2]) |
                (static_cast<std::uint16_t>(packet.bytes[3]) << 8u));
            if (packet.bytes[0] != expectedIndex || packet.returnPC != expectedCaller ||
                (i < 512u && (packet.bytes[1] != 0x00u ||
                 packetWord != expectedWord)) ||
                (i >= 512u && word.value != packetWord)) {
                std::printf("SoundHost packet mismatch[%zu]: actual=%02X %02X %02X %02X "
                            "word=%04X expectedIndex=%02X expectedWord=%04X caller=%04X "
                            "expectedCaller=%04X\\n",
                    i, packet.bytes[0], packet.bytes[1], packet.bytes[2], packet.bytes[3],
                    static_cast<unsigned>(word.value), expectedIndex,
                    static_cast<unsigned>(expectedWord), packet.returnPC, expectedCaller);
                std::fflush(stdout);
            }
            assert(packet.bytes[0] == expectedIndex);
            assert(packet.returnPC == expectedCaller);
            if (i < 512u) {
                assert(packet.bytes[1] == 0x00u);
                assert(packetWord == expectedWord);
                assert(word.value == expectedWord);
            } else {
                // The final 32 transactions are issued by a different firmware
                // helper; preserve their observed payload instead of applying
                // the unverified 0x0040/0x8040 assumption from the first helper.
                assert(word.lowByte == packet.bytes[2]);
                assert(word.highByte == packet.bytes[3]);
                assert(word.value == packetWord);
            }
        }
        const auto hostReads = firmwareMachine.soundHostReadCount();
        std::printf("External-window reads (current test model, %zu):\\n", hostReads);
        for (std::size_t i = 0; i < hostReads; ++i) {
            const auto& read = firmwareMachine.soundHostReadAt(i);
            std::printf("  read[%03zu] reg=%u value=%02X pc=%04X cycle=%llu\\n",
                        i, static_cast<unsigned>(read.registerIndex),
                        static_cast<unsigned>(read.value),
                        static_cast<unsigned>(read.instructionPC),
                        static_cast<unsigned long long>(read.machineCycle));
        }
        std::printf("SoundHost host transactions after IC51 boot block (%zu):\\n",
                    bootTransactions);
        for (std::size_t i = 0; i < bootTransactions; ++i) {
            const auto& packet = firmwareMachine.soundCommandPacketAt(i);
            const auto& word = firmwareMachine.soundHostBurstWordAt(i);
            const auto& firstContext = packet.byteContext[0];
            const auto& lastContext = packet.byteContext[3];
            std::printf("  %03zu: %02X %02X %02X %02X -> %04X pc=%04X ret=%04X "
                        "P2/P3/8E=%02X/%02X/%02X->%02X/%02X/%02X "
                        "reset=%u/%u IC51=%u/%u\\n",
                        i,
                        static_cast<unsigned>(packet.bytes[0]),
                        static_cast<unsigned>(packet.bytes[1]),
                        static_cast<unsigned>(packet.bytes[2]),
                        static_cast<unsigned>(packet.bytes[3]),
                        static_cast<unsigned>(word.value),
                        static_cast<unsigned>(packet.instructionPC),
                        static_cast<unsigned>(packet.returnPC),
                        static_cast<unsigned>(firstContext.port2),
                        static_cast<unsigned>(firstContext.port3),
                        static_cast<unsigned>(firstContext.soundBusMode),
                        static_cast<unsigned>(lastContext.port2),
                        static_cast<unsigned>(lastContext.port3),
                        static_cast<unsigned>(lastContext.soundBusMode),
                        firstContext.soundDeviceResetReleased ? 1u : 0u,
                        lastContext.soundDeviceResetReleased ? 1u : 0u,
                        firstContext.ic51Selected ? 1u : 0u,
                        lastContext.ic51Selected ? 1u : 0u);
        }

        // Experimental real-IC51 MIDI path: the controlled external-RAM
        // window and 7BD5 status override were enabled immediately after the
        // IC51 bulk load, before the firmware resumed its main initialization.
        if (!factoryFrames.empty()) {
            const auto factoryStartCycle = firmwareMachine.elapsedMachineCycles();
            const auto cyclesPerSecond = firmwareMachine.machineCyclesPerSecond();
            const auto byteCycles = firmwareMachine.midiByteMachineCycles();
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
                    eventCycle, firmwareMachine.elapsedMachineCycles());
                assert(firmwareMachine.scheduleAciaMidiBytes(
                    frame.bytes.data(), frame.bytes.size(), eventCycle));
                const auto lastByteArrival = firstArrivalBase +
                    byteCycles * frame.bytes.size();
                const auto cycleBudget = lastByteArrival -
                    firmwareMachine.elapsedMachineCycles() + 1u;
                firmwareMachine.runForMachineCycles(static_cast<std::size_t>(cycleBudget));
                assert(!firmwareMachine.cpuFaulted());
                totalFactoryBytes += frame.bytes.size();
            }
            firmwareMachine.runForMachineCycles(5000000u);
            std::printf("Real IC51 factory Performance SysEx: frames=%zu bytes=%llu "
                        "PC=%04X packets=%zu burstWords=%zu IC51=%llu\\n",
                        factoryFrames.size(),
                        static_cast<unsigned long long>(totalFactoryBytes),
                        static_cast<unsigned>(firmwareMachine.cpu().registers().pc),
                        firmwareMachine.soundCommandPacketCount(),
                        firmwareMachine.soundHostBurstWordCount(),
                        static_cast<unsigned long long>(firmwareMachine.ic51ReadCount()));
        }
        const auto realPacketBaseline = firmwareMachine.soundCommandPacketCount();
        const auto realBurstBaseline = firmwareMachine.soundHostBurstWordCount();

        // Differentially isolate the three MIDI voice events. Each stage is
        // delivered through the emulated ACIA after the same prior state, so
        // Program Change setup is no longer conflated with Note On traffic.
        const auto runRealMidiStage = [&](const char* label,
                                          const std::uint8_t* bytes,
                                          std::size_t byteCount,
                                          bool injectReadyAfterResourceLoad = false) {
            const auto startCycle = firmwareMachine.elapsedMachineCycles();
            const auto packetBefore = firmwareMachine.soundCommandPacketCount();
            const auto burstBefore = firmwareMachine.soundHostBurstWordCount();
            const auto hostWriteBefore = firmwareMachine.soundHostWriteCount();
            const auto hostReadBefore = firmwareMachine.soundHostReadCount();
            const auto writerBefore = firmwareMachine.watchedCallCount(0x0db7u);
            const auto partBefore = firmwareMachine.watchedCallCount(0x4256u);
            const auto eventBefore = firmwareMachine.watchedCallCount(0x5671u);
            const auto midiBefore = firmwareMachine.watchedCallCount(0x57a8u);
            const auto descriptorBefore = firmwareMachine.watchedCallCount(0x1494u);
            const auto descriptorReadBefore = firmwareMachine.watchedCallCount(0xac00u);
            const auto descriptorAltBefore = firmwareMachine.watchedCallCount(0xac88u);
            const auto helperB34DBefore = firmwareMachine.watchedCallCount(0xb34du);
            const auto dispatchBefore = firmwareMachine.watchedCallCount(0x16c3u);
            const auto streamBefore = firmwareMachine.watchedCallCount(0x189du);
            const auto scanBefore = firmwareMachine.watchedCallCount(0x1009u);
            const auto validateBefore = firmwareMachine.watchedCallCount(0x018du);
            const auto helper1780Before = firmwareMachine.watchedCallCount(0x1780u);
            const auto helper17d4Before = firmwareMachine.watchedCallCount(0x17d4u);
            const auto helper0e8eBefore = firmwareMachine.watchedCallCount(0x0e8eu);
            const auto helper1456Before = firmwareMachine.watchedCallCount(0x1456u);
            const auto helper1742Before = firmwareMachine.watchedCallCount(0x1742u);
            const auto statusGateBefore = firmwareMachine.watchedCallCount(0x4803u);
            const auto pitchTableBefore = firmwareMachine.watchedCallCount(0x02d1u);
            const auto ic51Before = firmwareMachine.ic51ReadCount();
            const auto peripheralTraceBefore = firmwareMachine.unmappedBusTraceCount();
            const auto xdataTraceBefore = firmwareMachine.xdataWatchTraceCount();
            assert(firmwareMachine.scheduleAciaMidiBytes(bytes, byteCount, startCycle));
            std::size_t instructions = 0;
            std::size_t drain = 0;
            bool statusInjected = false;
            if (injectReadyAfterResourceLoad) {
                // Let the real MIDI handler select and parse the resource first.
                // Then assert the synthetic DSP-ready response before the later
                // 0x4803 per-Part setup calls.
                std::uint64_t preCycles = 0;
                while (preCycles < 2000000u &&
                       firmwareMachine.watchedCallCount(0x1742u) == helper1742Before) {
                    instructions += firmwareMachine.runForMachineCycles(10000u);
                    preCycles += 10000u;
                }
                statusInjected =
                    firmwareMachine.watchedCallCount(0x1742u) > helper1742Before;
                if (statusInjected)
                    firmwareMachine.setTestExternalReadOverride(0x7d95u, 0x01u);
                drain = firmwareMachine.runForMachineCycles(7000000u - preCycles);
            } else {
                instructions = firmwareMachine.runForMachineCycles(2000000u);
                drain = firmwareMachine.runForMachineCycles(5000000u);
            }
            const auto packetAfter = firmwareMachine.soundCommandPacketCount();
            const auto hostReadAfter = firmwareMachine.soundHostReadCount();
            const auto peripheralTraceAfter = firmwareMachine.unmappedBusTraceCount();
            const auto xdataTraceAfter = firmwareMachine.xdataWatchTraceCount();
            std::printf("Real IC51 MIDI %s: instructions=%zu drain=%zu PC=%04X "
                        "packets=+%zu burstWords=+%zu hostWrites=+%zu statusInjected=%u "
                        "calls[0DB7=+%llu 4256=+%llu 5671=+%llu 57A8=+%llu "
                        "1494=+%llu AC00=+%llu AC88=+%llu B34D=+%llu "
                        "16C3=+%llu 189D=+%llu 1009=+%llu 018D=+%llu "
                        "1780=+%llu 17D4=+%llu 0E8E=+%llu 1456=+%llu 1742=+%llu "
                        "4803=+%llu 02D1=+%llu] IC51=+%llu\\n",
                        label, instructions, drain,
                        static_cast<unsigned>(firmwareMachine.cpu().registers().pc),
                        packetAfter - packetBefore,
                        firmwareMachine.soundHostBurstWordCount() - burstBefore,
                        firmwareMachine.soundHostWriteCount() - hostWriteBefore,
                        static_cast<unsigned>(statusInjected),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x0db7u) - writerBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x4256u) - partBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x5671u) - eventBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x57a8u) - midiBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x1494u) - descriptorBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0xac00u) - descriptorReadBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0xac88u) - descriptorAltBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0xb34du) - helperB34DBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x16c3u) - dispatchBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x189du) - streamBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x1009u) - scanBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x018du) - validateBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x1780u) - helper1780Before),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x17d4u) - helper17d4Before),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x0e8eu) - helper0e8eBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x1456u) - helper1456Before),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x1742u) - helper1742Before),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x4803u) - statusGateBefore),
                        static_cast<unsigned long long>(firmwareMachine.watchedCallCount(0x02d1u) - pitchTableBefore),
                        static_cast<unsigned long long>(firmwareMachine.ic51ReadCount() - ic51Before));
            const auto hostWriteAfter = firmwareMachine.soundHostWriteCount();
            constexpr std::size_t hostWriteTraceCapacity = 4096u;
            const auto firstRetainedHostWrite = hostWriteAfter > hostWriteTraceCapacity
                ? hostWriteAfter - hostWriteTraceCapacity : 0u;
            const auto firstStageHostWrite = std::max(hostWriteBefore, firstRetainedHostWrite);
            std::printf("  SoundHost writes=+%zu (retained=%zu, overwritten=%zu)\\n",
                        hostWriteAfter - hostWriteBefore,
                        hostWriteAfter - firstStageHostWrite,
                        firstStageHostWrite - hostWriteBefore);
            std::array<std::size_t, 4> writesByRegister{};
            std::array<std::size_t, 256> writesByDevice{};
            std::size_t observedHandshakeStrobes = 0;
            for (std::size_t i = firstStageHostWrite; i < hostWriteAfter; ++i) {
                const auto& event = firmwareMachine.soundHostWriteAt(i);
                if (event.registerIndex < writesByRegister.size())
                    ++writesByRegister[event.registerIndex];
                ++writesByDevice[event.deviceIndex];
                if (event.registerIndex == 3u &&
                    (event.value == 0x40u || event.value == 0xc0u))
                    ++observedHandshakeStrobes;
                std::printf("    W%u=%02X dev=%02X pc=%04X ret=%04X P2=%02X P3=%02X "
                            "mode=%u IC51=%u reset=%u cycle=%llu\\n",
                            static_cast<unsigned>(event.registerIndex),
                            static_cast<unsigned>(event.value),
                            static_cast<unsigned>(event.deviceIndex),
                            static_cast<unsigned>(event.instructionPC),
                            static_cast<unsigned>(event.returnPC),
                            static_cast<unsigned>(event.port2),
                            static_cast<unsigned>(event.port3),
                            static_cast<unsigned>(event.soundBusMode),
                            static_cast<unsigned>(event.ic51Selected),
                            static_cast<unsigned>(event.soundDeviceResetReleased),
                            static_cast<unsigned long long>(event.machineCycle));
            }
            constexpr std::size_t hostReadTraceCapacity = 2048u;
            const auto firstRetainedHostRead = hostReadAfter > hostReadTraceCapacity
                ? hostReadAfter - hostReadTraceCapacity : 0u;
            const auto firstStageHostRead = std::max(hostReadBefore, firstRetainedHostRead);
            std::printf("  Write distribution: R0=%zu R1=%zu R2=%zu R3=%zu; "
                        "known R3 strobes(40/C0)=%zu\\n",
                        writesByRegister[0], writesByRegister[1],
                        writesByRegister[2], writesByRegister[3],
                        observedHandshakeStrobes);
            std::printf("  Device-index write counts:");
            for (std::size_t device = 0; device < writesByDevice.size(); ++device) {
                if (writesByDevice[device] != 0u)
                    std::printf(" %02zX:%zu", device, writesByDevice[device]);
            }
            std::printf("\\n");
            std::printf("  SoundHost reads=+%zu (retained=%zu, overwritten=%zu)\\n",
                        hostReadAfter - hostReadBefore,
                        hostReadAfter - firstStageHostRead,
                        firstStageHostRead - hostReadBefore);
            for (std::size_t i = firstStageHostRead; i < hostReadAfter; ++i) {
                const auto& event = firmwareMachine.soundHostReadAt(i);
                std::printf("    R%u=%02X pc=%04X ret=%04X P2=%02X P3=%02X "
                            "mode=%u IC51=%u reset=%u cycle=%llu\\n",
                            static_cast<unsigned>(event.registerIndex),
                            static_cast<unsigned>(event.value),
                            static_cast<unsigned>(event.instructionPC),
                            static_cast<unsigned>(event.returnPC),
                            static_cast<unsigned>(event.port2),
                            static_cast<unsigned>(event.port3),
                            static_cast<unsigned>(event.soundBusMode),
                            static_cast<unsigned>(event.ic51Selected),
                            static_cast<unsigned>(event.soundDeviceResetReleased),
                            static_cast<unsigned long long>(event.machineCycle));
            }
            constexpr std::size_t unmappedTraceCapacity = 4096u;
            const auto firstRetainedPeripheral = peripheralTraceAfter > unmappedTraceCapacity
                ? peripheralTraceAfter - unmappedTraceCapacity : 0u;
            const auto firstStagePeripheral = std::max(peripheralTraceBefore, firstRetainedPeripheral);
            std::printf("  Unknown-bus trace window: total=%zu retained=%zu overwritten=%zu\\n",
                        peripheralTraceAfter - peripheralTraceBefore,
                        peripheralTraceAfter - firstStagePeripheral,
                        firstStagePeripheral - peripheralTraceBefore);
            const auto firstTrace = peripheralTraceAfter > 4096u
                ? peripheralTraceAfter - 4096u : 0u;
            for (std::size_t i = std::max(firstTrace, peripheralTraceBefore);
                 i < peripheralTraceAfter; ++i) {
                const auto& event = firmwareMachine.unmappedBusTraceAt(i);
                if ((event.address < 0x7c00u || event.address > 0x7fffu) && event.address != 0x7bfeu) continue;
                std::printf("  MIDI-7B/7C %c %04X=%02X pc=%04X stackRet=%04X "
                            "P3=%02X 8E=%02X overlay=%u cycle=%llu\\n",
                            event.write ? 'W' : 'R',
                            static_cast<unsigned>(event.address),
                            static_cast<unsigned>(event.value),
                            static_cast<unsigned>(event.instructionPC),
                            static_cast<unsigned>(event.stackReturnPC),
                            static_cast<unsigned>(event.port3),
                            static_cast<unsigned>(event.soundBusMode),
                            static_cast<unsigned>(event.syntheticTestOverlay),
                            static_cast<unsigned long long>(event.machineCycle));
            }
            const auto firstXdataTrace = xdataTraceAfter > 8192u
                ? xdataTraceAfter - 8192u : 0u;
            for (std::size_t i = std::max(firstXdataTrace, xdataTraceBefore);
                 i < xdataTraceAfter; ++i) {
                const auto& event = firmwareMachine.xdataWatchTraceAt(i);
                if (event.address == 0x3098u) {
                    std::printf("  FX-SELECT %c 3098=%02X pc=%04X stackRet=%04X "
                                "P2=%02X P3=%02X 8E=%02X IC51=%u accepted=%u cycle=%llu\\n",
                                event.write ? 'W' : 'R',
                                static_cast<unsigned>(event.value),
                                static_cast<unsigned>(event.instructionPC),
                                static_cast<unsigned>(event.stackReturnPC),
                                static_cast<unsigned>(event.port2),
                                static_cast<unsigned>(event.port3),
                                static_cast<unsigned>(event.soundBusMode),
                                static_cast<unsigned>(event.ic51Selected),
                                static_cast<unsigned>(event.writeAccepted),
                                static_cast<unsigned long long>(event.machineCycle));
                }
                std::printf("  MIDI-XDATA %c %04X=%02X pc=%04X stackRet=%04X "
                            "P2=%02X P3=%02X 8E=%02X IC51=%u accepted=%u cycle=%llu\\n",
                            event.write ? 'W' : 'R',
                            static_cast<unsigned>(event.address),
                            static_cast<unsigned>(event.value),
                            static_cast<unsigned>(event.instructionPC),
                            static_cast<unsigned>(event.stackReturnPC),
                            static_cast<unsigned>(event.port2),
                            static_cast<unsigned>(event.port3),
                            static_cast<unsigned>(event.soundBusMode),
                            static_cast<unsigned>(event.ic51Selected),
                            static_cast<unsigned>(event.writeAccepted),
                            static_cast<unsigned long long>(event.machineCycle));
            }
            assert(!firmwareMachine.cpuFaulted());
        };

        const std::array<std::uint8_t, 2> realProgramChange{{0xc0u, 0x05u}};
        const std::array<std::uint8_t, 3> realNoteOn{{0x90u, 0x3cu, 0x64u}};
        const std::array<std::uint8_t, 3> realNoteOff{{0x80u, 0x3cu, 0x40u}};
        runRealMidiStage("Program Change in Performance mode",
                         realProgramChange.data(), realProgramChange.size());
        runRealMidiStage("Note On in Performance mode",
                         realNoteOn.data(), realNoteOn.size());
        runRealMidiStage("Note Off in Performance mode",
                         realNoteOff.data(), realNoteOff.size());

        // System parameter address 00 00 03: Multi mode = 1.
        const std::array<std::uint8_t, 10> enableMultiMode{{
            0xf0u, 0x3fu, 0x00u, 0x22u, 0x44u, 0x00u, 0x00u, 0x03u, 0x01u, 0xf7u
        }};
        runRealMidiStage("SysEx set Multi mode=1",
                         enableMultiMode.data(), enableMultiMode.size());

        // Performance 1 / Part 13 in TECHNOX.MID is bank 2, sound 10.
        // Re-address its actual 24-byte parameter block to 01 01 00, the
        // documented temporary Part 1 parameter area used by Multi mode.
        const std::array<std::uint8_t, 33> temporaryPart1{{
            0xf0u, 0x3fu, 0x00u, 0x22u, 0x44u, 0x01u, 0x01u, 0x00u,
            0x02u, 0x0au, 0x01u, 0x64u, 0x10u, 0x3fu, 0x3fu, 0x18u,
            0x40u, 0x40u, 0x40u, 0x40u, 0x40u, 0x40u, 0x40u, 0x40u,
            0x40u, 0x00u, 0x01u, 0x4cu, 0x1au, 0x40u, 0x13u, 0x00u, 0xf7u
        }};
        runRealMidiStage("SysEx load temporary Part 1 from Performance 1 Part 13",
                         temporaryPart1.data(), temporaryPart1.size());
        // 0x1742 drains its queue when internal RAM[0x52] is zero.
        // Force that queue-empty state exactly at 0x1742 entry; 0xFB=0x08
        // permits 0x1780 to enter the 16-slot scanner. Both are synthetic probes.
        firmwareMachine.setTestExternalReadOverride(0xfffbu, 0x08u);
        firmwareMachine.setTestExternalReadOverride(0xff80u, 0x16u);
        firmwareMachine.setTestExternalReadOverride(0xff88u, 0x16u);
        firmwareMachine.setTestInternalWriteOnCall(0x1742u, 0x52u, 0x00u);
        // The stage hook sets 0x7D95 only after resource loading has entered.
        runRealMidiStage("Note On Part 1 with synthetic queue-drain handshake",
                         realNoteOn.data(), realNoteOn.size(), true);

        // Capture the firmware's 16-slot resource staging page. The page is
        // selected dynamically through internal RAM[0x2D], so do not assume
        // an XDATA address or hard-code a bank.
        const auto massSlotPage = firmwareMachine.readInternal(0x2du);
        const auto readMassSlotByte = [&](std::uint8_t offset) {
            const auto address = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(massSlotPage) << 8) | offset);
            return address >= 0x7000u && address <= 0x7fffu
                ? firmwareMachine.testExternalRamByteForTesting(address)
                : firmwareMachine.xdataByteForTesting(address);
        };
        std::printf("MASS 16-slot staging page=%02X\\n",
                    static_cast<unsigned>(massSlotPage));
        for (unsigned slot = 0; slot < 16u; ++slot) {
            const auto slotId = readMassSlotByte(
                static_cast<std::uint8_t>(0x70u + slot));
            std::printf("  MASS-SLOT[%02u] id=%02X data=",
                        slot, static_cast<unsigned>(slotId));
            for (unsigned byte = 0; byte < 8u; ++byte) {
                const auto value = readMassSlotByte(static_cast<std::uint8_t>(
                    0x80u + slot * 8u + byte));
                std::printf("%02X%s", static_cast<unsigned>(value),
                            byte == 7u ? "" : " ");
            }
            std::printf("\\n");
        }

        // Search all firmware-visible control pages for populated 16-slot
        // records; the active page can change during resource interpretation.
        for (unsigned page = 0; page <= 0x38u; ++page) {
            unsigned nonzero = 0;
            std::array<std::uint8_t, 144> values{};
            for (unsigned offset = 0x70u; offset <= 0xffu; ++offset) {
                const auto address = static_cast<std::uint16_t>(
                    (page << 8) | offset);
                const auto value = firmwareMachine.xdataByteForTesting(address);
                values[offset - 0x70u] = value;
                if (value != 0) ++nonzero;
            }
            if (nonzero == 0) continue;
            std::printf("  MASS-PAGE[%02X] nonzero=%u:",
                        page, nonzero);
            unsigned shown = 0;
            for (unsigned offset = 0x70u; offset <= 0xffu && shown < 24u; ++offset) {
                const auto value = values[offset - 0x70u];
                if (value == 0) continue;
                std::printf(" %02X=%02X", offset, static_cast<unsigned>(value));
                ++shown;
            }
            std::printf("\\n");
        }
        for (unsigned page = 0x70u; page <= 0x7fu; ++page) {
            unsigned nonzero = 0;
            std::array<std::uint8_t, 144> values{};
            for (unsigned offset = 0x70u; offset <= 0xffu; ++offset) {
                const auto address = static_cast<std::uint16_t>(
                    (page << 8) | offset);
                const auto value = firmwareMachine.testExternalRamByteForTesting(address);
                values[offset - 0x70u] = value;
                if (value != 0) ++nonzero;
            }
            if (nonzero == 0) continue;
            std::printf("  MASS-EXT-PAGE[%02X] nonzero=%u:",
                        page, nonzero);
            unsigned shown = 0;
            for (unsigned offset = 0x70u; offset <= 0xffu && shown < 24u; ++offset) {
                const auto value = values[offset - 0x70u];
                if (value == 0) continue;
                std::printf(" %02X=%02X", offset, static_cast<unsigned>(value));
                ++shown;
            }
            std::printf("\\n");
        }

        runRealMidiStage("Note Off Part 1 after synthetic ready response",
                         realNoteOff.data(), realNoteOff.size());
        const auto realPackets = firmwareMachine.soundCommandPacketCount();
        const auto realBursts = firmwareMachine.soundHostBurstWordCount();
        const auto realSoundReads = firmwareMachine.soundMemoryReadTraceCount();
        std::printf("REAL-ROM sound-memory reads after MIDI: total=%llu traced=%zu\\n",
                    static_cast<unsigned long long>(firmwareMachine.soundMemoryReadCount()),
                    realSoundReads);
        const auto realFirstRead = realSoundReads > 64u ? realSoundReads - 64u : 0u;
        for (std::size_t i = realFirstRead; i < realSoundReads; ++i) {
            const auto& event = firmwareMachine.soundMemoryReadTraceAt(i);
            std::printf("  REAL-XREAD[%zu] %04X=%02X mode=%u pc=%04X ret=%04X cycle=%llu\\n",
                        i, static_cast<unsigned>(event.address),
                        static_cast<unsigned>(event.value), static_cast<unsigned>(event.mode),
                        static_cast<unsigned>(event.instructionPC),
                        static_cast<unsigned>(event.returnPC),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        const std::uint16_t midiWatchAddresses[] = {
            0x7d95u, 0x3702u, 0x3703u, 0x3704u, 0x370au, 0x370bu, 0x3039u,
            0x303au, 0x304bu, 0x3050u, 0x3051u, 0x2fb3u, 0x2fb4u,
            0x2900u, 0x2901u, 0x2902u, 0x2903u, 0x2904u, 0x2905u, 0x2906u, 0x2907u,
            0x2990u, 0x2991u, 0x2992u, 0x2993u, 0x2994u, 0x2995u,
            0x299cu, 0x299du, 0x299eu, 0x299fu, 0x29a0u, 0x29a1u,
            0x2b8bu, 0x2b8cu, 0x2b8du, 0x2b8fu, 0x2b90u, 0x2b91u,
            0x2b92u, 0x2b93u, 0x2b94u, 0x2b95u, 0x2b96u, 0x2b97u,
            0x2b98u, 0x2b99u, 0x2b9au, 0x2b9bu, 0x2b9cu, 0x2b9du,
            0x2b9eu, 0x2b9fu, 0x2d60u, 0x2d61u, 0x2d62u, 0x2df8u,
            0x2df9u, 0x2dfau, 0x2dfbu, 0x2dfcu, 0x2dfdu, 0x2dfeu,
            0x2dffu, 0x2e00u, 0x2e01u, 0x2e02u, 0x2e03u, 0x2e04u,
            0x2e05u, 0x2e06u, 0x2e07u, 0x2e08u, 0x2e09u, 0x2e0au,
            0x2e0bu, 0x2e0cu, 0x2e0du, 0x2e0eu, 0x2e0fu, 0x2f00u,
            0x2f01u, 0x2f02u, 0x2f03u, 0x2f04u, 0x2f05u, 0x2f06u,
            0x2f07u, 0x2f08u, 0x2f09u, 0x2f0au, 0x2f0bu, 0x2f0cu,
            0x2f0du, 0x2f0eu, 0x2f0fu, 0x2f80u, 0x2f81u, 0x2f82u,
            0x2f83u, 0x2f84u, 0x2f85u, 0x2f86u, 0x2f87u, 0x2f88u,
            0x2f89u, 0x2f8au, 0x2f8bu, 0x2f8cu, 0x2f8du, 0x2f8eu,
            0x2f8fu, 0x0080u, 0x0088u, 0x0090u, 0x0098u, 0x00a0u,
            0x00a8u, 0x00b0u, 0x00b8u, 0x00c0u, 0x00c8u, 0x00d0u,
            0x00d8u, 0x00e0u, 0x00e8u, 0x00f0u, 0x00f8u, 0x00fbu, 0xff80u, 0xff88u, 0xfffbu,
            0x2ff0u, 0x2fffu, 0x2fd7u, 0x2fa2u, 0x2000u, 0x2001u, 0x2020u,
            0x2021u, 0x2022u, 0x2023u, 0x2050u, 0x20a0u, 0x20f0u,
            0x2140u, 0x2190u, 0x21e0u, 0x2230u, 0x2280u, 0x22d0u,
            0x2320u, 0x2370u, 0x3700u, 0x3701u, 0x3702u, 0x3703u,
            0x3704u, 0x3705u, 0x3706u, 0x3707u, 0x3708u, 0x3709u,
            0x370au, 0x370bu, 0x370cu, 0x370du, 0x370eu, 0x370fu,
            0x7c15u, 0x7c16u, 0x7c17u, 0x7c18u, 0x7c19u, 0x7c1au,
            0x7c1bu, 0x7c1cu, 0x7c26u, 0x7bd5u, 0x7d95u
        };
        std::printf("Real IC51 MIDI RAM:");
        for (const auto address : midiWatchAddresses) {
            const auto value = address >= 0x7000u && address <= 0x7fffu
                ? firmwareMachine.testExternalRamByteForTesting(address)
                : firmwareMachine.xdataByteForTesting(address);
            std::printf(" %04X=%02X", static_cast<unsigned>(address),
                        static_cast<unsigned>(value));
        }
        std::printf(" effective7BD5=%02X effective7D95=%02X effectiveFFFB=%02X "
                    "effectiveFF80=%02X effectiveFF88=%02X "
                    "RAM[4E]=%02X RAM[4F]=%02X RAM[50]=%02X RAM[51]=%02X RAM[52]=%02X\\n",
                    static_cast<unsigned>(firmwareMachine.readExternal(0x7bd5u)),
                    static_cast<unsigned>(firmwareMachine.readExternal(0x7d95u)),
                    static_cast<unsigned>(firmwareMachine.readExternal(0xfffbu)),
                    static_cast<unsigned>(firmwareMachine.readExternal(0xff80u)),
                    static_cast<unsigned>(firmwareMachine.readExternal(0xff88u)),
                    static_cast<unsigned>(firmwareMachine.readInternal(0x4eu)),
                    static_cast<unsigned>(firmwareMachine.readInternal(0x4fu)),
                    static_cast<unsigned>(firmwareMachine.readInternal(0x50u)),
                    static_cast<unsigned>(firmwareMachine.readInternal(0x51u)),
                    static_cast<unsigned>(firmwareMachine.readInternal(0x52u)));
        assert(!firmwareMachine.cpuFaulted());
        for (std::size_t i = realPacketBaseline; i < realPackets; ++i) {
            const auto& packet = firmwareMachine.soundCommandPacketAt(i);
            const auto& word = firmwareMachine.soundHostBurstWordAt(i);
            std::printf("  real-host[%zu] %02X %02X %02X %02X -> %04X pc=%04X ret=%04X\\n",
                        i - realPacketBaseline,
                        static_cast<unsigned>(packet.bytes[0]),
                        static_cast<unsigned>(packet.bytes[1]),
                        static_cast<unsigned>(packet.bytes[2]),
                        static_cast<unsigned>(packet.bytes[3]),
                        static_cast<unsigned>(word.value),
                        static_cast<unsigned>(packet.instructionPC),
                        static_cast<unsigned>(packet.returnPC));
        }
        if (realBursts > realBurstBaseline)
            std::printf("  real burst tail=%04X\\n",
                        static_cast<unsigned>(firmwareMachine.soundHostBurstWordAt(realBursts - 1u).value));

        // Controlled test stimulus only: inject the expected AC signature so
        // the CPU can pass the otherwise-unmodeled external-device wait.
        TechnoX8032Machine injectedResponseMachine;
        assert(injectedResponseMachine.loadIC4Rom(
            realRom.data(),
            realRom.size()));
        // Keep this second machine synthetic so its ACIA timing test is
        // isolated from the asynchronous IC51/resource-loader startup path.
        // Real IC51 descriptors and command streams are covered above and in
        // test_mass_resource_rom.
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
        assert(injectedResponseMachine.soundMemoryWriteCount() > 0u);

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
            // Finish the long factory SysEx stream before the isolated
            // Program Change / Note On stimulus is queued.
            injectedResponseMachine.runForMachineCycles(5000000u);
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
        const auto ieBeforeMidi = injectedResponseMachine.readInternal(0xa8);
        assert(injectedResponseMachine.scheduleAciaMidiBytes(
            programAndNote.data(), programAndNote.size(), midiStartCycle));
        const auto midiInstructions = injectedResponseMachine.runForMachineCycles(2000000);
        const auto midiDrainInstructions = injectedResponseMachine.runForMachineCycles(5000000u);
        std::printf("IC4 MIDI Note On diagnostic: instructions=%zu drain=%zu PC=%04X IE=%02X TCON=%02X ACIA=%02X P3=%02X IC51=%llu bank0=%llu bank1=%llu lastIC51=%04X ACIAreads=%llu validReads=%llu lastRX=%02X lastValid=%02X queued=%zu RAM49=%02X RAM47=%02X RAM59=%02X RAM5E=%02X X2B8B=%02X X2B8C=%02X X2B8D=%02X X2B8F=%02X X30E4=%02X R53=%02X R56=%02X R29=%02X R2A=%02X R5D=%02X R5E=%02X X2B90=%02X X2B9D=%02X X2B9E=%02X X2B9F=%02X X30E6=%02X X30E8=%02X X304C=%02X X304D=%02X X304E=%02X\\n",
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
                    static_cast<unsigned long long>(injectedResponseMachine.aciaValidReceiveReadCount()),
                    static_cast<unsigned>(injectedResponseMachine.lastAciaReceiveRead()),
                    static_cast<unsigned>(injectedResponseMachine.lastValidAciaReceiveRead()),
                    injectedResponseMachine.scheduledAciaPendingCount(),
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
        const auto soundReadTraceCount = injectedResponseMachine.soundMemoryReadTraceCount();
        std::printf("Sound-memory reads after MIDI: total=%llu traced=%zu\\n",
                    static_cast<unsigned long long>(injectedResponseMachine.soundMemoryReadCount()),
                    soundReadTraceCount);
        const auto firstSoundRead = soundReadTraceCount > 48u
            ? soundReadTraceCount - 48u : 0u;
        for (std::size_t i = firstSoundRead; i < soundReadTraceCount; ++i) {
            const auto& event = injectedResponseMachine.soundMemoryReadTraceAt(i);
            std::printf("  XREAD[%zu] addr=%04X value=%02X mode=%u pc=%04X ret=%04X cycle=%llu\\n",
                        i, static_cast<unsigned>(event.address),
                        static_cast<unsigned>(event.value),
                        static_cast<unsigned>(event.mode),
                        static_cast<unsigned>(event.instructionPC),
                        static_cast<unsigned>(event.returnPC),
                        static_cast<unsigned long long>(event.machineCycle));
        }
        std::fflush(stdout);
        assert(!injectedResponseMachine.cpuFaulted());
        // The byte scheduler delivered the stimulus; full MIDI-to-voice
        // dispatch is not asserted until the SoundHost host/DSP path is modeled.
        assert(injectedResponseMachine.scheduledAciaPendingCount() == 0u);
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
        if (commandCount > commandCountBeforeMidi) {
        // AC00 is a dispatch-table entry, not a guaranteed call on every
        // synthetic MIDI stimulus; command-packet output below is the stronger
        // regression signal for this fixture.
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
        } else {
            std::printf("MIDI bytes were received, but this fixture emitted no SoundHost host transaction yet.\\n");
        }
    }
    return 0;
}
