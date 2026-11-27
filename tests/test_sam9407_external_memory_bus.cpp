#include "../Source/SAM9407ExternalMemoryBus.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace technox;

int main() {
    static_assert(SAM9407ExternalMemoryBus::kMaximumWordAddress == 0x01ffffffu);
    static_assert(SAM9407ExternalMemoryBus::kAddressSpaceWordCount == 0x02000000ull);
    static_assert(SAM9407ExternalMemoryBus::isValidImageByteCount(2u));
    static_assert(SAM9407ExternalMemoryBus::isValidImageByteCount(0x04000000ull));
    static_assert(!SAM9407ExternalMemoryBus::isValidImageByteCount(0u));
    static_assert(!SAM9407ExternalMemoryBus::isValidImageByteCount(1u));
    static_assert(!SAM9407ExternalMemoryBus::isValidImageByteCount(0x04000001ull));
    static_assert(!SAM9407ExternalMemoryBus::isValidImageByteCount(0x04000002ull));

    // S1/S0 meanings from the SAM9407 pin description:
    // 00 idle, 01 synthesis access, 10 instruction fetch, 11 P16 read/write.
    assert(SAM9407ExternalMemoryBus::decodeCycle(false, false) ==
           SAM9407MemoryCycle::idle);
    assert(SAM9407ExternalMemoryBus::decodeCycle(false, true) ==
           SAM9407MemoryCycle::synthesisAccess);
    assert(SAM9407ExternalMemoryBus::decodeCycle(true, false) ==
           SAM9407MemoryCycle::instructionFetch);
    assert(SAM9407ExternalMemoryBus::decodeCycle(true, true) ==
           SAM9407MemoryCycle::processorReadWrite);
    assert(SAM9407ExternalMemoryBus::encodeS1(
        SAM9407MemoryCycle::instructionFetch));
    assert(!SAM9407ExternalMemoryBus::encodeS0(
        SAM9407MemoryCycle::instructionFetch));

    std::array<std::uint8_t, 8> romBytes{{
        0x34, 0x12, 0xcd, 0xab, 0x00, 0x80, 0xff, 0x7f
    }};
    MASSWaveformMemoryView romView;
    assert(romView.attach(romBytes.data(), romBytes.size()));

    SAM9407ExternalMemoryBus bus;
    assert(bus.addReadOnlyRegion(0x00100000u, 4u,
        SAM9407MemoryDevice::pcmRom, romView));
    assert(bus.regionCount() == 1u);
    assert(bus.containsReadableRange(0x00100000u, 0x00100004u));
    assert(!bus.containsReadableRange(0x000fffffu, 0x00100004u));
    assert(!bus.containsReadableRange(0x00100000u, 0x00100005u));

    std::uint16_t word = 0;
    assert(!bus.readWord(0x00100000u, SAM9407MemoryCycle::idle, word));
    assert(bus.readWord(0x00100000u,
        SAM9407MemoryCycle::synthesisAccess, word));
    assert(word == 0x1234u);
    assert(bus.readWord(0x00100001u,
        SAM9407MemoryCycle::instructionFetch, word));
    assert(word == 0xabcdu);
    assert(bus.readWord(0x00100003u,
        SAM9407MemoryCycle::processorReadWrite, word));
    assert(word == 0x7fff);
    assert(!bus.readWord(0x00100004u,
        SAM9407MemoryCycle::synthesisAccess, word));
    assert(!bus.writeWord(0x00100000u,
        SAM9407MemoryCycle::processorReadWrite, 0xffffu));
    assert(romBytes[0] == 0x34 && romBytes[1] == 0x12);

    // Two adjacent chip-select windows are accepted; overlapping windows are not.
    assert(bus.addWritableRegion(0x00100004u, 2u,
        SAM9407MemoryDevice::sram, nullptr, 0u) == false);
    assert(bus.regionCount() == 1u);
    std::array<std::uint8_t, 8> ramBytes{};
    assert(bus.addWritableRegion(0x00100004u, 4u,
        SAM9407MemoryDevice::sram, ramBytes.data(), ramBytes.size()));
    assert(!bus.addWritableRegion(0x00100006u, 2u,
        SAM9407MemoryDevice::dram, ramBytes.data(), ramBytes.size()));
    assert(bus.regionCount() == 2u);
    assert(bus.writeWord(0x00100004u,
        SAM9407MemoryCycle::processorReadWrite, 0x5aa5u));
    assert(ramBytes[0] == 0xa5u && ramBytes[1] == 0x5au);
    assert(bus.readWord(0x00100004u,
        SAM9407MemoryCycle::synthesisAccess, word));
    assert(word == 0x5aa5u);
    assert(bus.writeWord(0x00100005u,
        SAM9407MemoryCycle::synthesisAccess, 0x1234u));
    assert(!bus.writeWord(0x00100005u,
        SAM9407MemoryCycle::instructionFetch, 0xffffu));

    // The highest 25-bit word address is legal; one word beyond it is not.
    std::array<std::uint8_t, 4> topBytes{{0x78, 0x56, 0xbc, 0x9a}};
    MASSWaveformMemoryView topView;
    assert(topView.attach(topBytes.data(), topBytes.size()));
    SAM9407ExternalMemoryBus topBus;
    assert(topBus.addReadOnlyRegion(0x01fffffeu, 2u,
        SAM9407MemoryDevice::pcmRom, topView));
    assert(topBus.readWord(0x01ffffffu,
        SAM9407MemoryCycle::synthesisAccess, word));
    assert(word == 0x9abcu);
    assert(!topBus.addReadOnlyRegion(0x01ffffffu, 2u,
        SAM9407MemoryDevice::pcmRom, topView));
    assert(!topBus.readWord(0x02000000u,
        SAM9407MemoryCycle::synthesisAccess, word));

    // Device typing is strict: a PCM ROM cannot be registered as writable RAM,
    // and a RAM device cannot use the immutable ROM-view registration path.
    SAM9407ExternalMemoryBus typeBus;
    assert(!typeBus.addReadOnlyRegion(0u, 4u,
        SAM9407MemoryDevice::dram, topView));
    assert(!typeBus.addWritableRegion(0u, 2u,
        SAM9407MemoryDevice::pcmRom, ramBytes.data(), ramBytes.size()));

    bus.clear();
    assert(bus.regionCount() == 0u);
    assert(!bus.readWord(0x00100000u,
        SAM9407MemoryCycle::processorReadWrite, word));
    return 0;
}
