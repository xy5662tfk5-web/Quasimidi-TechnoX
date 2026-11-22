#include "../Source/MASSResourceROM.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

using namespace technox;

namespace {
void writePointerBE(std::vector<std::uint8_t>& image,
                    MASSResourceBank bank,
                    std::uint8_t index,
                    std::uint16_t address) {
    const auto base = bank == MASSResourceBank::bank0 ? 0u : MASSResourceROM::kBankSize;
    const auto offset = base + static_cast<std::size_t>(index) * 2;
    image[offset] = static_cast<std::uint8_t>(address >> 8);
    image[offset + 1] = static_cast<std::uint8_t>(address & 0xff);
}

void testPointerTableAndPartMapping() {
    std::vector<std::uint8_t> image(MASSResourceROM::kImageSize, 0xff);
    writePointerBE(image, MASSResourceBank::bank0, 5, 0x1234);
    writePointerBE(image, MASSResourceBank::bank1, 7, 0xabcd);

    MASSResourceROM rom;
    assert(!rom.isLoaded());
    assert(!rom.load(image.data(), image.size() - 1));
    assert(rom.load(image.data(), image.size()));
    assert(rom.isLoaded());

    std::uint16_t address = 0;
    assert(rom.readResourceDescriptorAddress(MASSResourceBank::bank0, 5, address));
    assert(address == 0x1234);
    assert(rom.readResourceDescriptorAddress(MASSResourceBank::bank1, 7, address));
    assert(address == 0xabcd);

    MASSResourceSelection selection{};
    assert(MASSResourceROM::resolvePartResource(0, 0x35, selection));
    assert(selection.bank == MASSResourceBank::bank0);
    assert(selection.pointerIndex == 0x35 && selection.p3BankMask == 0x07);
    assert(MASSResourceROM::resolvePartResource(1, 0x35, selection));
    assert(selection.bank == MASSResourceBank::bank0);
    assert(selection.pointerIndex == 0xb5 && selection.p3BankMask == 0x07);
    assert(MASSResourceROM::resolvePartResource(2, 0x35, selection));
    assert(selection.bank == MASSResourceBank::bank1);
    assert(selection.pointerIndex == 0x35 && selection.p3BankMask == 0x06);
    assert(MASSResourceROM::resolvePartResource(3, 0x35, selection));
    assert(selection.bank == MASSResourceBank::bank1);
    assert(selection.pointerIndex == 0xb5 && selection.p3BankMask == 0x06);
    assert(MASSResourceROM::resolvePartResource(9, 0x35, selection));
    assert(selection.normalizedPartParameter == 3 && selection.pointerIndex == 0xb5);
    assert(!MASSResourceROM::resolvePartResource(-1, 0x35, selection));

    rom.clear();
    assert(!rom.isLoaded());
    assert(!rom.readResourceDescriptorAddress(MASSResourceBank::bank0, 0, address));
}

void testDescriptorParser() {
    std::vector<std::uint8_t> bank(0x10000, 0xff);
    constexpr std::uint16_t base = 0x0200;
    for (std::size_t i = 0; i < 8; ++i)
        bank[base + i] = static_cast<std::uint8_t>(0xa0 + i);

    std::size_t p = base + 8;
    bank[p++] = 0x00; // Table A group 0
    bank[p++] = 0x01; bank[p++] = 0xaa; bank[p++] = 0xbb;
    bank[p++] = 0x7f; bank[p++] = 0xcc; bank[p++] = 0xdd; // group terminator
    bank[p++] = 0x05; // Table A terminator
    bank[p++] = 0x00; bank[p++] = 0x11; bank[p++] = 0x22;
    bank[p++] = 0x05; bank[p++] = 0x33; bank[p++] = 0x44;
    bank[p++] = 0x06; // Table B terminator
    bank[p++] = 0x00; bank[p++] = 0x12; // little-endian stream pointer 0x1200

    MASSResourceDescriptor descriptor{};
    assert(MASSResourceDescriptorParser::parse(
        bank.data(), bank.size(), base, descriptor));
    assert(descriptor.descriptorAddress == base);
    assert(descriptor.fixedHeader[0] == 0xa0 && descriptor.fixedHeader[7] == 0xa7);
    assert(descriptor.tableA.size() == 1);
    assert(descriptor.tableA[0].groupId == 0);
    assert(descriptor.tableA[0].entries.size() == 2);
    assert(descriptor.tableA[0].entries[0].selector == 0x01);
    assert(descriptor.tableA[0].entries[0].data0 == 0xaa);
    assert(descriptor.tableA[0].entries[1].selector == 0x7f);
    assert(descriptor.tableATerminator == 0x05);
    assert(descriptor.tableB.size() == 2);
    assert(descriptor.tableB[0].key == 0x00);
    assert(descriptor.tableB[1].key == 0x05);
    assert(descriptor.tableBTerminator == 0x06);
    assert(descriptor.commandStreamPointer == 0x1200);
}

void testCommandStreamDecoder() {
    std::vector<std::uint8_t> bank(0x10000, 0xff);
    bank[0x1200] = 0x18;
    bank[0x1201] = 0xaa;
    bank[0x1202] = 0xbb;
    bank[0x1203] = 0xcc;
    bank[0x1204] = 0x08;
    bank[0x1205] = 0x01;
    bank[0x1206] = 0x02;
    bank[0x1207] = 0x03;
    bank[0x1208] = 0x04;
    bank[0x1209] = 0x05;
    bank[0x120a] = 0x06;
    bank[0x120b] = 0xff;

    MASSDecodedResourceStream decoded{};
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1200, decoded));
    assert(decoded.terminated && !decoded.truncated);
    assert(decoded.slotsConsumed == 2 && decoded.commands.size() == 2);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::fourByteCommand);
    assert(decoded.commands[0].length == 4 && decoded.commands[0].bytes[3] == 0xcc);
    assert(decoded.commands[1].kind == MASSResourceCommandKind::pitchParameter);
    assert(decoded.commands[1].length == 7 && decoded.commands[1].bytes[6] == 0x06);
    assert(decoded.bytesConsumed == 11);

    bank[0x1300] = 0x10;
    bank[0x1301] = 0x00;
    bank[0x1302] = 0x11;
    bank[0x1303] = 0x22;
    bank[0x1304] = 0x33;
    bank[0x1305] = 0x44;
    bank[0x1306] = 0x55;
    bank[0x1307] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1300, decoded));
    assert(decoded.terminated && decoded.slotsConsumed == 1);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::emptyMultiParameter);
    assert(decoded.commands[0].length == 7 && decoded.commands[0].bytes[6] == 0x55);

    // Group 0x00 has two paths: byte +3 bit 3 chooses a seven-byte
    // hardware-routed path unless firmware flag 0x0A is set.
    bank[0x1400] = 0x00;
    bank[0x1401] = 0x11;
    bank[0x1402] = 0x22;
    bank[0x1403] = 0x08;
    bank[0x1404] = 0x31;
    bank[0x1405] = 0x42;
    bank[0x1406] = 0x53;
    bank[0x1407] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1400, decoded));
    assert(decoded.terminated && decoded.slotsConsumed == 1);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::conditionalRecord);
    assert(decoded.commands[0].length == 7 && decoded.commands[0].routesToHardware);

    bank[0x1450] = 0x00;
    bank[0x1451] = 0x11;
    bank[0x1452] = 0x22;
    bank[0x1453] = 0x08;
    bank[0x1454] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1450, decoded, true));
    assert(decoded.terminated && decoded.slotsConsumed == 1);
    assert(decoded.commands[0].length == 4 && !decoded.commands[0].routesToHardware);

    bank[0x1500] = 0x3b; // 0x38 filler/slot marker, not the end-of-stream byte
    bank[0x1501] = 0x28; // group 0x28 advances by one byte
    bank[0x1502] = 0x30; // group 0x30 advances by one byte
    bank[0x1503] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1500, decoded));
    assert(decoded.terminated && decoded.slotsConsumed == 3);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::marker);
    assert(decoded.commands[1].kind == MASSResourceCommandKind::singleByteAdvance);
    assert(decoded.commands[2].kind == MASSResourceCommandKind::singleByteAdvance);

    for (std::size_t i = 0; i < MASSCommandStreamDecoder::kMaximumSlots; ++i)
        bank[0x1600 + i] = 0x28;
    bank[0x1610] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1600, decoded));
    assert(!decoded.terminated && decoded.slotLimitReached);
    assert(decoded.slotsConsumed == MASSCommandStreamDecoder::kMaximumSlots);

    bank[0xfffc] = 0x08;
    bank[0xfffd] = 0x01;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0xfffc, decoded));
    assert(decoded.truncated && !decoded.terminated);
    assert(decoded.commands.empty());
}

void testActualIC51(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    assert(image.size() == MASSResourceROM::kImageSize);

    MASSResourceROM rom;
    assert(rom.load(image.data(), image.size()));

    std::uint16_t address = 0;
    assert(rom.readResourceDescriptorAddress(MASSResourceBank::bank0, 0, address));
    assert(address == 0x24d4);
    assert(rom.readResourceDescriptorAddress(MASSResourceBank::bank1, 0, address));
    assert(address == 0x0f5b);

    for (const auto bank : {MASSResourceBank::bank0, MASSResourceBank::bank1}) {
        std::set<std::uint16_t> pointers;
        for (std::size_t i = 0; i < MASSResourceROM::kPointerCount; ++i) {
            assert(rom.readResourceDescriptorAddress(
                bank, static_cast<std::uint8_t>(i), address));
            assert(pointers.insert(address).second);
        }
        assert(pointers.size() == MASSResourceROM::kPointerCount);
    }
}
}

int main(int argc, char** argv) {
    testPointerTableAndPartMapping();
    testDescriptorParser();
    testCommandStreamDecoder();
    if (argc > 1)
        testActualIC51(argv[1]);
    return 0;
}
