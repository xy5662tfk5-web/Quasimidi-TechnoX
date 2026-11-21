#include "../Source/TechnoXFirmwareTransaction.h"
#include "../Source/TechnoXMailboxProtocol.h"

#include <cassert>
#include <cstdint>

using namespace technox::firmware;

int main() {
    // 0x0DB7 writes R5, R2, R3, then R4 with ACC.6 cleared.
    // Internal flag 0x0C mirrors the original R4/ACC.7 bit.
    const auto mailboxLow = makeMailboxWrite(0x12, 0x34, 0x56, 0x7F);
    assert(mailboxLow.kBaseAddress == 0x8000);
    assert((mailboxLow.bytes == std::array<std::uint8_t, 4>{0x12, 0x34, 0x56, 0x3F}));
    assert(!mailboxLow.setInternalFlag0C);
    const auto mailboxHigh = makeMailboxWrite(0xA1, 0xB2, 0xC3, 0xFF);
    assert((mailboxHigh.bytes == std::array<std::uint8_t, 4>{0xA1, 0xB2, 0xC3, 0xBF}));
    assert(mailboxHigh.setInternalFlag0C);
    const auto mailboxFlagOnly = makeMailboxWrite(0, 0, 0, 0x80);
    assert(mailboxFlagOnly.bytes[3] == 0x80);
    assert(mailboxFlagOnly.setInternalFlag0C);
    const auto mailboxBit6Only = makeMailboxWrite(0, 0, 0, 0x40);
    assert(mailboxBit6Only.bytes[3] == 0x00);
    assert(!mailboxBit6Only.setInternalFlag0C);

    XDataMemory memory;
    XDataMemory::Page page{};
    page[0x20] = 0xE5; // first byte: high bit must be masked by the loader
    page[0x21] = 0x64; // nonzero second byte selects the nonzero path
    page[0x22] = 0x03;
    page[0x23] = 0x11;
    page[0x24] = 0x22;
    page[0x25] = 0x33;
    page[0x26] = 0x44;
    page[0x27] = 0xA5; // skipped
    page[0x28] = 0x5A; // skipped
    page[0x29] = 0x07; // only low three bits become flags
    page[0x2A] = 0x09;
    page[0x2B] = 0x55;
    memory.loadPage(0x29, page);

    LookupTable lookup{};
    lookup.available = true;
    lookup.values[0x03] = 0xA3;
    lookup.values[0x09] = 0xB9;

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
    assert(nonzeroPlan.slotDataAddress == 0x224D);
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


    // Firmware 0x1009 scans a packed list at P2=0x80, R0=0x80..0xF0,
    // in steps of eight. The first 0xFF terminates the list. At the 0x1045
    // call site R4 is 0x80 iff flag 0x0D is set (ACC.7 is copied into R4).
    XDataMemory mailboxMemory;
    XDataMemory::Page mailboxPage{};
    mailboxPage[0x80] = 0x16; // marker: (value & 0x7E) == 0x16
    mailboxPage[0x88] = 0x02; // occupied entry, but not a mailbox marker
    mailboxPage[0x90] = 0x97; // marker with high bit and low bit set
    mailboxPage[0x98] = 0xFF; // terminator; later entries must be ignored
    mailboxPage[0xA0] = 0x16;
    mailboxMemory.loadPage(kMailboxScanPage, mailboxPage);

    const auto mailboxScan = scanMailboxEntries(mailboxMemory, 7, true);
    assert(mailboxScan);
    assert(mailboxScan.visitedCount == 3);
    assert(mailboxScan.writeCount == 2);
    assert(mailboxScan.terminatedByEmpty);
    assert(mailboxScan.nextSequence == 10);
    assert(mailboxScan.visitedOffsets[0] == 0x80);
    assert(mailboxScan.visitedOffsets[1] == 0x88);
    assert(mailboxScan.visitedOffsets[2] == 0x90);
    assert((mailboxScan.writes[0].bytes == std::array<std::uint8_t, 4>{7, 0x80, 0x01, 0x80}));
    assert((mailboxScan.writes[1].bytes == std::array<std::uint8_t, 4>{9, 0x80, 0x01, 0x80}));
    assert(mailboxScan.writes[0].setInternalFlag0C);
    assert(mailboxScan.writes[1].setInternalFlag0C);

    const auto mailboxScanFlagClear = scanMailboxEntries(mailboxMemory, 7, false);
    assert(mailboxScanFlagClear.writeCount == 2);
    assert(mailboxScanFlagClear.writes[0].bytes[3] == 0x00);
    assert(!mailboxScanFlagClear.writes[0].setInternalFlag0C);

    const auto missingMailboxPage = scanMailboxEntries(XDataMemory{}, 0, false);
    assert(!missingMailboxPage);
    assert(missingMailboxPage.error == MailboxScanError::sourcePageMissing);

    return 0;
}
