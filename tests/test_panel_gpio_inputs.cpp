#include "../Source/TechnoX8032Machine.h"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
std::vector<std::uint8_t> readBinary(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void initialisePanelScanner(technox::TechnoX8032Machine& machine)
{
    auto& cpu = machine.cpuForTesting().registersForTesting();
    cpu.pc = 0xA6A8; // IC4's real 16-byte front-panel event FIFO initializer.
    const auto executed = machine.runUntilProgramCounter(0xA6D0, 256);
    assert(executed > 0);
    assert(cpu.pc == 0xA6D0);
    assert(!machine.cpuFaulted());
}

std::size_t runEncoderScan(technox::TechnoX8032Machine& machine,
                           std::uint8_t phaseAndDirection)
{
    // P1.5 is the encoder strobe/phase input. P1.6 is sampled as direction.
    machine.setExternalPortInputs(0x90u, 0x60u, phaseAndDirection);
    // The scan routine also samples P3.0 while it multiplexes the P1 outputs.
    machine.setExternalPortInputs(0xB0u, 0x01u, 0x01u);

    auto& cpu = machine.cpuForTesting().registersForTesting();
    cpu.pc = 0xA762; // Actual Timer-1 front-panel encoder scan routine in IC4.
    const auto executed = machine.runUntilProgramCounter(0xA79F, 512);
    assert(executed > 0);
    assert(cpu.pc == 0xA79F);
    assert(!machine.cpuFaulted());
    return machine.readInternal(0x17u); // IC4 FIFO event count.
}
}

int main(int argc, char** argv)
{
    // Preserve the direct electrical-input seam independently of firmware.
    technox::TechnoX8032Machine pinsOnly;
    pinsOnly.reset();
    pinsOnly.writeInternal(0x90u, 0xffu);
    pinsOnly.setExternalPortInputs(0x90u, 0x28u, 0x00u);
    auto p1 = pinsOnly.readInternal(0x90u);
    assert((p1 & 0x28u) == 0x00u);
    assert((p1 & 0xd7u) == 0xd7u);
    pinsOnly.setExternalPortInputs(0x90u, 0x28u, 0x28u);
    p1 = pinsOnly.readInternal(0x90u);
    assert((p1 & 0x28u) == 0x28u);
    assert((p1 & 0xd7u) == 0xd7u);

    pinsOnly.writeInternal(0xb0u, 0xffu);
    pinsOnly.setExternalPortInputs(0xb0u, 0x01u, 0x00u);
    assert((pinsOnly.readInternal(0xb0u) & 0x01u) == 0x00u);
    pinsOnly.setExternalPortInputs(0xb0u, 0x01u, 0x01u);
    assert((pinsOnly.readInternal(0xb0u) & 0x01u) == 0x01u);

    // Run the exact IC4 scan routine against the original, descrambled in-core.
    // This proves the two independent P1.5/P1.6 scan channels and their FIFO codes.
    if (argc < 2) {
        std::cerr << "Usage: test_panel_gpio_inputs <raw-ic4-rom>\\n";
        return 2;
    }
    const auto rom = readBinary(argv[1]);
    technox::TechnoX8032Machine machine;
    assert(machine.loadIC4Rom(rom.data(), rom.size()));
    machine.reset();

    // First establish both encoder strobe latches in the released/high state.
    initialisePanelScanner(machine);
    assert(runEncoderScan(machine, 0x60u) == 0u);

    // Pull P1.5 low while P1.6 is high: the ROM emits 7E (encoder 1)
    // followed by 7D (encoder 2).
    const auto highDirectionCount = runEncoderScan(machine, 0x40u);
    assert(highDirectionCount == 2u);
    assert(machine.readIndirect(0xa6u) == 0x7eu);
    assert(machine.readIndirect(0xa7u) == 0x7du);

    // A held-low phase is not repeated as a second detent.
    assert(runEncoderScan(machine, 0x40u) == 2u);

    // Release both strobe inputs, then assert the opposite direction.
    assert(runEncoderScan(machine, 0x60u) == 2u);
    const auto nextHighCount = runEncoderScan(machine, 0x00u);
    assert(nextHighCount == 4u);
    assert(machine.readIndirect(0xa8u) == 0xbeu);
    assert(machine.readIndirect(0xa9u) == 0xbdu);

    std::cout << "TechnoX IC4 panel GPIO and quadrature scan tests passed\\n";
    return 0;
}
