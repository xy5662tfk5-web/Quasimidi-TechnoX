#include "../Source/TechnoXFirmwareTransaction.h"
#include "../Source/TechnoXMailboxProtocol.h"

#include <cassert>
#include <cstdint>

using namespace technox::firmware;

int main() {
    // 0x0DB7 clears bit 6 of R4 before writing it, while E7 independently
    // controls internal flag 0x0C.
    const auto mailboxLow = makeMailboxWrite(0x12, 0x34, 0x56, 0x7F, false);
    assert(mailboxLow.kBaseAddress == 0x8000);
    assert((mailboxLow.bytes == std::array<std::uint8_t, 4>{0x12, 0x34, 0x56, 0x3F}));
    assert(!mailboxLow.setInternalFlag0C);
    const auto mailboxHigh = makeMailboxWrite(0xA1, 0xB2, 0xC3, 0xFF, true);
    assert((mailboxHigh.bytes == std::array<std::uint8_t, 4>{0xA1, 0xB2, 0xC3, 0xBF}));
    assert(mailboxHigh.setInternalFlag0C);
    const auto independentFlag = makeMailboxWrite(0, 0, 0, 0x00, true);
    assert(independentFlag.bytes[3] == 0x00);
    assert(independentFlag.setInternalFlag0C);
    const auto highR4WithoutFlag = makeMailboxWrite(0, 0, 0, 0x80, false);
    assert(highR4WithoutFlag.bytes[3] == 0x80);
    assert(!highR4WithoutFlag.setInternalFlag0C);

    // 0x0DCE synchronizes internal bit 0x0C with E7, not with R4.
    const auto handshakeIdleLow = transitionMailboxHandshake(false, false);
    assert(!handshakeIdleLow.nextFlag0C && !handshakeIdleLow.writesFourthByte);
    const auto handshakeSet = transitionMailboxHandshake(false, true);
    assert(handshakeSet.nextFlag0C && handshakeSet.writesFourthByte);
    assert(handshakeSet.fourthByte == 0xC0);
    const auto handshakeIdleHigh = transitionMailboxHandshake(true, true);
    assert(handshakeIdleHigh.nextFlag0C && !handshakeIdleHigh.writesFourthByte);
    const auto handshakeClear = transitionMailboxHandshake(true, false);
    assert(!handshakeClear.nextFlag0C && handshakeClear.writesFourthByte);
    assert(handshakeClear.fourthByte == 0x40);

    XDataMemory memory;
    XDataMemory::Page page{};
    page[0x20] = 0xE5;
    page[0x21] = 0x64;
    page[0x22] = 0x03;
    page[0x23] = 0x11;
    page[0x24] = 0x22;
    page[0x25] = 0x33;
    page[0x26] = 0x44;
    page[0x27] = 0xA5; // skipped descriptor byte 7
    page[0x28] = 0x5A; // skipped descriptor byte 8
    page[0x29] = 0x33; // skipped descriptor byte 9
    page[0x2A] = 0x07; // byte 10: low three bits become flags
    page[0x2B] = 0x09; // byte 11: translated through ROM table 0x1208
    page[0x2C] = 0x55; // byte 12: copied directly
    memory.loadPage(0x29, page);

    LookupTable lookup{};
    lookup.available = true;
    lookup.values[0x03] = 0xA3;
    lookup.values[0x09] = 0xB9;

    // Verify the real ROM table at 0x1208 independently of the injectable test table.
    const auto romLookup = makeTechnoXLookupTable();
    assert(romLookup.available);
    assert(romLookup.values[0x00] == 0x00);
    assert(romLookup.values[0x03] == 0x01);
    assert(romLookup.values[0x09] == 0x03);
    assert(romLookup.values[0x40] == 0x1F);
    assert(romLookup.values[0x7F] == 0x7F);

    const auto romDecoded = decodeDescriptor(memory, 0x29, 0x20, romLookup);
    assert(romDecoded);
    assert(romDecoded.descriptor.mappedThird == 0x01);
    assert(romDecoded.descriptor.mappedEleventh == 0x03);

    const auto decoded = decodeDescriptor(memory, 0x29, 0x20, lookup);
    assert(decoded);
    assert(decoded.descriptor.first() == 0x65);
    assert(decoded.descriptor.second == 0x64);
    assert(decoded.path == LoaderPath::nonzeroSecondByte);
    assert(decoded.descriptor.mappedThird == 0xA3);
    assert(decoded.descriptor.fourth == 0x11);
    assert(decoded.descriptor.fifth == 0x22);
    assert(decoded.descriptor.sixth == 0x33);
    assert(decoded.descriptor.seventh == 0x44);
    assert(decoded.descriptor.controlFlags == 0x07);
    assert(decoded.descriptor.bit0() && decoded.descriptor.bit1() && decoded.descriptor.bit2());
    assert(decoded.descriptor.mappedEleventh == 0xB9);
    assert(decoded.descriptor.twelfth == 0x55);

    page[0x21] = 0;
    memory.loadPage(0x29, page);
    const auto zeroPath = decodeDescriptor(memory, 0x29, 0x20, lookup);
    assert(zeroPath);
    assert(zeroPath.path == LoaderPath::zeroSecondByte);

    const auto missingPage = decodeDescriptor(memory, 0x2A, 0x20, lookup);
    assert(!missingPage);
    assert(missingPage.error == DecodeError::sourcePageMissing);

    const auto crossing = decodeDescriptor(memory, 0x29, 0xF8, lookup);
    assert(!crossing);
    assert(crossing.error == DecodeError::descriptorCrossesPage);

    LookupTable unavailable{};
    const auto noLookup = decodeDescriptor(memory, 0x29, 0x20, unavailable);
    assert(!noLookup);
    assert(noLookup.error == DecodeError::lookupTableUnavailable);

    const auto decision = prepareLoaderDecision(memory, lookup, {0x29, 0x20, 0x09});
    assert(decision.valid);
    assert(decision.input.mode == 0x09);
    assert(decision.path == LoaderPath::zeroSecondByte);

    page[0x21] = 0x64;
    memory.loadPage(0x29, page);
    const auto nonzeroDecision = prepareLoaderDecision(memory, lookup, {0x29, 0x20, 0x03});
    assert(nonzeroDecision.valid);
    const auto nonzeroPlan = planLoaderBranch(nonzeroDecision,
        LoaderRuntimeFlags{true, false, false, true});
    assert(nonzeroPlan.valid);
    assert(nonzeroPlan.slotBase == 0x21E8);
    assert(nonzeroPlan.slotDataAddress == 0x21E8); // bit 0x06 skips the descriptor offset

    const auto nonzeroPlanWithoutBit06 = planLoaderBranch(nonzeroDecision,
        LoaderRuntimeFlags{false, false, false, true});
    assert(nonzeroPlanWithoutBit06.valid);
    assert(nonzeroPlanWithoutBit06.slotDataAddress == 0x224D); // bit 0x06 clear adds byte 0

    assert(nonzeroPlan.actionCount == 4);
    assert(nonzeroPlan.actions[0] == LoaderAction::callResourceManager17D4);
    assert(nonzeroPlan.actions[1] == LoaderAction::setSlotBusyBit);
    assert(nonzeroPlan.actions[2] == LoaderAction::writeSecondByte);
    assert(nonzeroPlan.actions[3] == LoaderAction::continueAt1599);

    page[0x21] = 0;
    memory.loadPage(0x29, page);
    const auto zeroDecision = prepareLoaderDecision(memory, lookup, {0x29, 0x20, 0x03});
    const auto zeroControlPlan = planLoaderBranch(zeroDecision,
        LoaderRuntimeFlags{false, false, false, false});
    assert(zeroControlPlan.actionCount == 2);
    assert(zeroControlPlan.actions[0] == LoaderAction::readModeControlByte);
    assert(zeroControlPlan.actions[1] == LoaderAction::jumpToZeroByteHandler);

    const auto zeroScanPlan = planLoaderBranch(zeroDecision,
        LoaderRuntimeFlags{true, false, false, false});
    assert(zeroScanPlan.actionCount == 3);
    assert(zeroScanPlan.actions[2] == LoaderAction::returnToModeControl);

    // 0x1009 uses the XDATA page selected by RAM[0x2D]. At call site 0x1045,
    // R4 is zero and E7 is set iff internal flag 0x0D is set.
    XDataMemory mailboxMemory;
    XDataMemory::Page mailboxPage{};
    mailboxPage[0x80] = 0x16;
    mailboxPage[0x88] = 0x02;
    mailboxPage[0x90] = 0x97;
    mailboxPage[0x98] = 0xFF;
    mailboxPage[0xA0] = 0x16;
    mailboxMemory.loadPage(0x29, mailboxPage);

    const auto mailboxScan = scanMailboxEntries(mailboxMemory, 0x29, 7, true);
    assert(mailboxScan);
    assert(mailboxScan.visitedCount == 3);
    assert(mailboxScan.writeCount == 2);
    assert(mailboxScan.terminatedByEmpty);
    assert(mailboxScan.nextSequence == 10);
    assert(mailboxScan.visitedOffsets[0] == 0x80);
    assert(mailboxScan.visitedOffsets[1] == 0x88);
    assert(mailboxScan.visitedOffsets[2] == 0x90);
    assert((mailboxScan.writes[0].bytes == std::array<std::uint8_t, 4>{7, 0x80, 0x01, 0x00}));
    assert((mailboxScan.writes[1].bytes == std::array<std::uint8_t, 4>{9, 0x80, 0x01, 0x00}));
    assert(mailboxScan.writes[0].setInternalFlag0C);
    assert(mailboxScan.writes[1].setInternalFlag0C);

    const auto mailboxScanFlagClear = scanMailboxEntries(mailboxMemory, 0x29, 7, false);
    assert(mailboxScanFlagClear.writeCount == 2);
    assert(mailboxScanFlagClear.writes[0].bytes[3] == 0x00);
    assert(!mailboxScanFlagClear.writes[0].setInternalFlag0C);

    const auto missingMailboxPage = scanMailboxEntries(XDataMemory{}, 0x29, 0, false);
    assert(!missingMailboxPage);
    assert(missingMailboxPage.error == MailboxScanError::sourcePageMissing);

    return 0;
}
