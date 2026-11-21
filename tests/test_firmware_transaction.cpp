#include "../Source/TechnoXFirmwareTransaction.h"

#include <cassert>
#include <cstdint>

using namespace technox::firmware;

int main() {
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
    assert(nonzeroPlan.actions[2] == LoaderAction::writeSecondByte ||
           nonzeroPlan.actions[2] == LoaderAction::continueAt1599);

    page[0x21] = 0;\n    memory.loadPage(0x29, page);\n    const auto zeroDecision = prepareLoaderDecision(memory, lookup, {0x29, 0x20, 0x03});
    const auto zeroControlPlan = planLoaderBranch(zeroDecision,
        LoaderRuntimeFlags{false, false, false, false});
    assert(zeroControlPlan.actionCount == 2);
    assert(zeroControlPlan.actions[0] == LoaderAction::readModeControlByte);
    assert(zeroControlPlan.actions[1] == LoaderAction::jumpToZeroByteHandler);

    return 0;
}
