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

void testFirmwareROMTableView() {
    std::vector<std::uint8_t> synthetic(0x20000, 0xff);
    synthetic[0x1288] = 0x12;
    synthetic[0x1308 + 0x12] = 0x58;
    synthetic[0x11e0] = 0x80;
    MASSFirmwareROMView view;
    assert(!view.isAttached());
    assert(!view.attach(nullptr, synthetic.size()));
    assert(!view.attach(synthetic.data(), 0x1407));
    assert(view.attach(synthetic.data(), synthetic.size()));
    std::uint8_t value = 0;
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E0, 0, value) && value == 0x80);
    assert(view.lookupNonlinearScale(0, value) && value == 0x58);
    assert(!view.read(MASSFirmwareLookupTable::nonlinearScale1288, 128, value));
    assert(!view.read(MASSFirmwareLookupTable::quantizeMap1308, 256, value));
    view.detach();
    assert(!view.isAttached());
    assert(!view.read(MASSFirmwareLookupTable::nonlinearScale1288, 0, value));
}

void testActualFirmwareROMTables(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assert(image.size() == 0x20000);
    MASSFirmwareROMView view;
    assert(view.attach(image.data(), image.size()));
    std::uint8_t value = 0;
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E0, 0, value) && value == 0x80);
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E0, 7, value) && value == 0xf0);
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E8, 0, value) && value == 0x10);
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E8, 7, value) && value == 0xe0);
    assert(view.read(MASSFirmwareLookupTable::interpolationCurve11F0, 0, value) && value == 0x80);
    assert(view.read(MASSFirmwareLookupTable::interpolationCurve11F0, 11, value) && value == 0xf2);
    assert(view.read(MASSFirmwareLookupTable::interpolationCurve11FC, 0, value) && value == 0xff);
    assert(view.read(MASSFirmwareLookupTable::interpolationCurve11FC, 11, value) && value == 0x88);
    assert(view.read(MASSFirmwareLookupTable::parameterMap1208, 0, value) && value == 0x00);
    assert(view.read(MASSFirmwareLookupTable::parameterMap1208, 127, value) && value == 0x7f);
    assert(view.read(MASSFirmwareLookupTable::nonlinearScale1288, 0, value) && value == 0x00);
    assert(view.read(MASSFirmwareLookupTable::nonlinearScale1288, 127, value) && value == 0xff);
    assert(view.read(MASSFirmwareLookupTable::quantizeMap1308, 0, value) && value == 0x00);
    assert(view.read(MASSFirmwareLookupTable::quantizeMap1308, 255, value) && value == 0xf8);
    for (const auto table : {MASSFirmwareLookupTable::parameterMap1208,
                             MASSFirmwareLookupTable::nonlinearScale1288,
                             MASSFirmwareLookupTable::quantizeMap1308}) {
        std::uint8_t previous = 0;
        for (std::size_t i = 0; i < MASSFirmwareROMView::tableLength(table); ++i) {
            assert(view.read(table, i, value));
            if (i != 0) assert(value >= previous);
            previous = value;
        }
    }
    for (std::size_t index = 0; index < 128; ++index)
        assert(view.lookupNonlinearScale(static_cast<std::uint8_t>(index), value));
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

void testResourceSelectionRecordParser() {
    const auto part12Address = MASSResourceSelectionRecordParser::addressForRuntimePart(12);
    assert(part12Address.page == 0x29 && part12Address.offset == 0x90);
    assert(part12Address.linearAddress() == 0x2990);
    const auto part15Address = MASSResourceSelectionRecordParser::addressForRuntimePart(15);
    assert(part15Address.page == 0x29 && part15Address.offset == 0xb4);

    std::array<std::uint8_t, 20> page{};
    const std::array<std::uint8_t, 12> record = {
        0x92, 0x31, 0x15, 0x44, 0x55, 0x24, 0xd4, 0x77, 0x88, 0x05, 0x2a, 0x66
    };
    std::copy(record.begin(), record.end(), page.begin() + 4);

    MASSResourceSelectionRecord parsed{};
    assert(MASSResourceSelectionRecordParser::parse(
        page.data(), page.size(), 4, parsed));
    assert(parsed.selectionCode() == 0x12);
    assert(parsed.descriptorAddress() == 0x24d4);
    assert(parsed.p3ControlFlags() == 0x05);
    assert(parsed.lookupIndex0() == 0x15);
    assert(parsed.lookupIndex1() == 0x2a);

    // MOVX @R0 increments only the low byte. A record beginning at 0xFA
    // wraps from 0xFF to 0x00 while remaining on the P2-selected page.
    std::array<std::uint8_t, 256> selectedPage{};
    const std::array<std::uint8_t, 12> wrappedRecord = {
        0x82, 0x31, 0x12, 0x44, 0x55, 0x24, 0xd4, 0x77, 0x88, 0x06, 0x2a, 0x66
    };
    for (std::size_t i = 0; i < wrappedRecord.size(); ++i)
        selectedPage[static_cast<std::uint8_t>(0xfa + i)] = wrappedRecord[i];
    MASSResourceSelectionRecord wrapped{};
    const MASSXDataAddress wrappedAddress{0x29, 0xfa};
    assert(MASSResourceSelectionRecordParser::parseSelectedPage(
        selectedPage.data(), selectedPage.size(), wrappedAddress, wrapped));
    assert(wrapped.xdataAddress.page == 0x29 && wrapped.xdataAddress.offset == 0xfa);
    assert(wrapped.bytes[0] == 0x82 && wrapped.bytes[6] == 0xd4 &&
           wrapped.bytes[9] == 0x06 && wrapped.bytes[11] == 0x66);
    assert(!MASSResourceSelectionRecordParser::parseSelectedPage(
        selectedPage.data(), selectedPage.size() - 1, wrappedAddress, wrapped));

    assert(!MASSResourceSelectionRecordParser::parse(
        page.data(), page.size(), 9, parsed));
    assert(!MASSResourceSelectionRecordParser::parse(
        nullptr, page.size(), 0, parsed));
}

void testCommandRegisterFieldDecoders() {
    MASSResourceCommand command{};
    command.opcode = 0x0d;
    command.kind = MASSResourceCommandKind::pitchParameter;
    command.length = 7;
    command.bytes = {{0x0d, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76}};

    MASSPitchParameterRegisterFields pitch{};
    assert(MASSResourceCommandFieldDecoder::decodePitchParameter(command, pitch));
    assert(pitch.opcode == 0x0d && pitch.r2 == 0x21 && pitch.r3 == 0x32);
    assert(pitch.r4 == 0x43 && pitch.r7 == 0x54 &&
           pitch.r6 == 0x65 && pitch.r5 == 0x76);

    command.opcode = 0x12;
    command.kind = MASSResourceCommandKind::multiParameter;
    command.bytes = {{0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78}};
    MASSMultiParameterRegisterFields multi{};
    assert(MASSResourceCommandFieldDecoder::decodeMultiParameter(command, multi));
    assert(multi.opcode == 0x12 && multi.r3 == 0x23 && multi.r5 == 0x34);
    assert(multi.r6 == 0x45 && multi.f0 == 0x56 &&
           multi.r4 == 0x67 && multi.r7 == 0x78);
    assert(!multi.takesFillerPath());

    command.bytes[1] = 0;
    command.kind = MASSResourceCommandKind::emptyMultiParameter;
    assert(MASSResourceCommandFieldDecoder::decodeMultiParameter(command, multi));
    assert(multi.takesFillerPath());

    command.opcode = 0x22;
    assert(!MASSResourceCommandFieldDecoder::decodeMultiParameter(command, multi));
}

void testMultiParameterPacketSequenceBuilder() {
    MASSResourceCommand command{};
    command.opcode = 0x15;
    command.kind = MASSResourceCommandKind::multiParameter;
    command.length = 7;
    command.bytes = {{0x15, 0x01, 0x12, 0x23, 0x34, 0x45, 0x56}};

    MASSResolvedMultiParameterRegisters registers{};
    registers.ram2B = 0x40;
    registers.r2 = 0x81;
    registers.r3 = 0x92;
    registers.r4 = 0x03;
    registers.r5 = 0xa4;
    registers.r6 = 0xb5;
    registers.r7 = 0xc6;

    MASSHardwareCommandPacketSequence sequence{};
    const std::array<std::uint8_t, 3> buffered{{0xd7, 0xe8, 0xf9}};
    assert(MASSResourceHardwareCommandBuilder::buildMultiParameterPackets(
        command, registers, false, buffered, sequence));
    assert(sequence.count == 2);
    assert((sequence.packets[0].bytes ==
            std::array<std::uint8_t, 4>{{0x40, 0xa4, 0xb5, 0xc6}}));
    assert((sequence.packets[1].bytes ==
            std::array<std::uint8_t, 4>{{0x3f, 0x81, 0x92, 0x03}}));

    assert(MASSResourceHardwareCommandBuilder::buildMultiParameterPackets(
        command, registers, true, buffered, sequence));
    assert(sequence.count == 3);
    assert((sequence.packets[0].bytes ==
            std::array<std::uint8_t, 4>{{0x3e, 0xd7, 0xe8, 0xf9}}));
    assert((sequence.packets[1].bytes ==
            std::array<std::uint8_t, 4>{{0x40, 0xa4, 0xb5, 0xc6}}));
    assert((sequence.packets[2].bytes ==
            std::array<std::uint8_t, 4>{{0x3f, 0x81, 0x92, 0x03}}));

    command.bytes[1] = 0;
    assert(!MASSResourceHardwareCommandBuilder::buildMultiParameterPackets(
        command, registers, false, buffered, sequence));
}

void testConditionalExtendedPacketBuilder() {
    MASSResourceCommand command{};
    command.opcode = 0x00;
    command.kind = MASSResourceCommandKind::conditionalRecord;
    command.length = 7;
    command.branchesToExtendedCommand = true;
    command.bytes = {{0x00, 0x11, 0x22, 0x08, 0xa1, 0xb2, 0xfd}};

    MASSHardwareCommandPacket packet{};
    assert(MASSResourceHardwareCommandBuilder::buildConditionalExtendedPacket(
        command, 0x30, false, packet));
    assert((packet.bytes == std::array<std::uint8_t, 4>{{0x30, 0xa1, 0xb2, 0x05}}));
    assert(!MASSResourceHardwareCommandBuilder::buildConditionalExtendedPacket(
        command, 0x30, true, packet));
    command.branchesToExtendedCommand = false;
    assert(!MASSResourceHardwareCommandBuilder::buildConditionalExtendedPacket(
        command, 0x30, false, packet));
}

void testFourByteParameterPacketBuilder() {
    MASSResourceCommand command{};
    command.opcode = 0x1b;
    command.kind = MASSResourceCommandKind::fourByteCommand;
    command.length = 4;
    command.bytes = {{0x1b, 0xa1, 0xb2, 0xfd}};

    MASSHardwareCommandPacket packet{};
    assert(MASSResourceHardwareCommandBuilder::buildFourByteParameterPacket(
        command, 0x30, false, packet));
    assert((packet.bytes == std::array<std::uint8_t, 4>{{0x30, 0xa1, 0xb2, 0x05}}));
    assert(!MASSResourceHardwareCommandBuilder::buildFourByteParameterPacket(
        command, 0x30, true, packet));

    command.opcode = 0x22;
    assert(!MASSResourceHardwareCommandBuilder::buildFourByteParameterPacket(
        command, 0x30, false, packet));
}

void testTerminalHardwarePacketBuilder() {
    MASSResourceCommand command{};
    command.opcode = 0x22; // (opcode & 0x38) == 0x20 terminal group
    command.kind = MASSResourceCommandKind::threePayloadCommand;
    command.length = 4;
    command.bytes = {{0x22, 0x11, 0x22, 0xfd}};

    MASSHardwareCommandPacket packet{};
    assert(MASSResourceHardwareCommandBuilder::buildTerminalPacket(
        command, 0x20, 0x91, 0x72, packet));
    assert((packet.bytes == std::array<std::uint8_t, 4>{{0x2f, 0x91, 0x72, 0x05}}));

    command.opcode = 0x18;
    assert(!MASSResourceHardwareCommandBuilder::buildTerminalPacket(
        command, 0x20, 0x91, 0x72, packet));
    command.opcode = 0x22;
    command.length = 3;
    assert(!MASSResourceHardwareCommandBuilder::buildTerminalPacket(
        command, 0x20, 0x91, 0x72, packet));
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
    assert(decoded.commands[0].length == 7 && decoded.commands[0].branchesToExtendedCommand);

    bank[0x1450] = 0x00;
    bank[0x1451] = 0x11;
    bank[0x1452] = 0x22;
    bank[0x1453] = 0x08;
    bank[0x1454] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1450, decoded, true));
    assert(decoded.terminated && decoded.slotsConsumed == 1);
    assert(decoded.commands[0].length == 4 && !decoded.commands[0].branchesToExtendedCommand);

    // Without flag 0x0A, a clear bit 3 in byte +3 takes the short four-byte
    // branch and does not enter handler 0x224C.
    bank[0x1480] = 0x00;
    bank[0x1481] = 0x11;
    bank[0x1482] = 0x22;
    bank[0x1483] = 0x00;
    bank[0x1484] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1480, decoded));
    assert(decoded.terminated && decoded.commands[0].length == 4);
    assert(!decoded.commands[0].branchesToExtendedCommand);

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

    // Group 0x20 is an interpreter-exit command: data after its four bytes
    // must not be consumed even if it resembles another valid command.
    bank[0x1700] = 0x20;
    bank[0x1701] = 0x11;
    bank[0x1702] = 0x22;
    bank[0x1703] = 0x03;
    bank[0x1704] = 0x28;
    bank[0x1705] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1700, decoded));
    assert(!decoded.terminated && decoded.endedByControlCommand);
    assert(decoded.fillsRemainingSlots);
    assert(decoded.slotsConsumed == 1 && decoded.commands.size() == 1);
    assert(decoded.outputSlotsWritten == 16 && decoded.fillerSlotsWritten == 15);
    assert(decoded.bytesConsumed == 4 && decoded.endOffset == 0x1704);

    bank[0x1710] = 0x20;
    bank[0x1711] = 0x11;
    bank[0x1712] = 0x22;
    bank[0x1713] = 0x03;
    bank[0x1714] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1710, decoded, true));
    assert(!decoded.terminated && decoded.endedByControlCommand);
    assert(!decoded.fillsRemainingSlots && decoded.bytesConsumed == 4);
    assert(decoded.outputSlotsWritten == 1 && decoded.fillerSlotsWritten == 0);

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

    const std::array<std::array<std::array<std::size_t, 3>, 2>, 2> expectedEndStates{{
        {{{159, 46, 51}, {165, 38, 53}}}, // bank 0: flag clear, then set
        {{{124, 55, 77}, {135, 60, 61}}}  // bank 1: flag clear, then set
    }};

    for (const auto bank : {MASSResourceBank::bank0, MASSResourceBank::bank1}) {
        std::set<std::uint16_t> pointers;
        const auto bankNumber = bank == MASSResourceBank::bank0 ? 0u : 1u;
        const auto* bankData = image.data() + bankNumber * MASSResourceROM::kBankSize;
        std::array<std::array<std::size_t, 3>, 2> endStates{};

        for (std::size_t i = 0; i < MASSResourceROM::kPointerCount; ++i) {
            assert(rom.readResourceDescriptorAddress(
                bank, static_cast<std::uint8_t>(i), address));
            assert(pointers.insert(address).second);

            MASSResourceDescriptor descriptor{};
            assert(MASSResourceDescriptorParser::parse(
                bankData, MASSResourceROM::kBankSize, address, descriptor));
            assert(descriptor.commandStreamPointer < MASSResourceROM::kBankSize);

            for (std::size_t flagIndex = 0; flagIndex < 2; ++flagIndex) {
                MASSDecodedResourceStream decoded{};
                assert(MASSCommandStreamDecoder::decode(
                    bankData, MASSResourceROM::kBankSize,
                    descriptor.commandStreamPointer, decoded, flagIndex != 0));
                assert(!decoded.truncated);
                if (decoded.endedByControlCommand) {
                    ++endStates[flagIndex][0];
                    if (!flagIndex) {
                        assert(decoded.fillsRemainingSlots);
                        assert(decoded.outputSlotsWritten == MASSCommandStreamDecoder::kMaximumSlots);
                    }
                } else if (decoded.terminated) {
                    ++endStates[flagIndex][1];
                } else if (decoded.slotLimitReached) {
                    ++endStates[flagIndex][2];
                } else {
                    assert(false && "resource stream ended without a firmware terminal state");
                }
            }
        }
        assert(pointers.size() == MASSResourceROM::kPointerCount);
        assert(endStates == expectedEndStates[bankNumber]);
    }
}
}

int main(int argc, char** argv) {
    testPointerTableAndPartMapping();
    testFirmwareROMTableView();
    if (argc > 2)
        testActualFirmwareROMTables(argv[2]);
    testResourceSelectionRecordParser();
    testCommandRegisterFieldDecoders();
    testMultiParameterPacketSequenceBuilder();
    testConditionalExtendedPacketBuilder();
    testFourByteParameterPacketBuilder();
    testTerminalHardwarePacketBuilder();
    testDescriptorParser();
    testCommandStreamDecoder();
    if (argc > 1)
        testActualIC51(argv[1]);
    return 0;
}
