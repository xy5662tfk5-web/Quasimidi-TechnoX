#pragma once

// Conservative executable model of the IC4 v1.05 transaction descriptor.
// This header intentionally has no JUCE dependency so it can be unit-tested
// independently from the plug-in and audio backend.

#include <array>
#include <cstddef>
#include <cstdint>

namespace technox::firmware {

constexpr std::size_t kPageCount = 256;
constexpr std::size_t kPageSize = 256;
constexpr std::size_t kDescriptorSize = 13;

class XDataMemory {
public:
    using Page = std::array<std::uint8_t, kPageSize>;

    bool read(std::uint8_t page, std::uint8_t offset, std::uint8_t& value) const noexcept {
        value = pages_[page][offset];
        return present_[page];
    }

    bool write(std::uint8_t page, std::uint8_t offset, std::uint8_t value) noexcept {
        pages_[page][offset] = value;
        present_[page] = true;
        return true;
    }

    bool loadPage(std::uint8_t page, const Page& bytes) noexcept {
        pages_[page] = bytes;
        present_[page] = true;
        return true;
    }

    bool pagePresent(std::uint8_t page) const noexcept { return present_[page]; }

private:
    std::array<Page, kPageCount> pages_{};
    std::array<bool, kPageCount> present_{};
};

struct Descriptor {
    std::uint8_t rawFirst = 0;
    std::uint8_t second = 0;
    std::uint8_t mappedThird = 0;
    std::uint8_t fourth = 0;
    std::uint8_t fifth = 0;
    std::uint8_t sixth = 0;
    std::uint8_t seventh = 0;
    std::uint8_t controlFlags = 0;
    std::uint8_t mappedEleventh = 0;
    std::uint8_t twelfth = 0;

    std::uint8_t first() const noexcept { return rawFirst & 0x7f; }
    bool bit0() const noexcept { return (controlFlags & 0x01) != 0; }
    bool bit1() const noexcept { return (controlFlags & 0x02) != 0; }
    bool bit2() const noexcept { return (controlFlags & 0x04) != 0; }
};

enum class LoaderPath : std::uint8_t {
    zeroSecondByte,
    nonzeroSecondByte
};

enum class DecodeError : std::uint8_t {
    none,
    sourcePageMissing,
    descriptorCrossesPage,
    lookupTableUnavailable
};

struct DecodeResult {
    Descriptor descriptor{};
    LoaderPath path = LoaderPath::zeroSecondByte;
    DecodeError error = DecodeError::none;

    explicit operator bool() const noexcept { return error == DecodeError::none; }
};

// The firmware's table at ROM 0x1208 is injected as a 256-entry lookup.
// Unknown/unavailable table contents are never silently fabricated.
struct LookupTable {
    std::array<std::uint8_t, 256> values{};
    bool available = false;
};

 // Exact 128-entry ROM lookup at IC4 address 0x1208, used twice by routine
 // 0x1494 to translate descriptor bytes 2 and 11.
inline constexpr std::array<std::uint8_t, 128> kTechnoXDescriptorValueMap{{
    0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09,
    0x0A, 0x0A, 0x0B, 0x0B, 0x0C, 0x0C, 0x0D, 0x0D, 0x0E, 0x0E, 0x0F, 0x0F, 0x10, 0x10, 0x11, 0x12,
    0x13, 0x13, 0x14, 0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x19, 0x1A, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
    0x1F, 0x20, 0x21, 0x23, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2B, 0x2C, 0x2E, 0x2F, 0x31, 0x32, 0x34,
    0x35, 0x36, 0x38, 0x39, 0x3B, 0x3C, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x44, 0x45, 0x47, 0x48, 0x4A,
    0x4B, 0x4D, 0x4E, 0x50, 0x51, 0x53, 0x54, 0x56, 0x57, 0x59, 0x5B, 0x5C, 0x5E, 0x60, 0x61, 0x63,
    0x65, 0x66, 0x68, 0x6A, 0x6B, 0x6D, 0x6F, 0x70, 0x72, 0x74, 0x76, 0x78, 0x7A, 0x7C, 0x7E, 0x7F
}};

inline LookupTable makeTechnoXLookupTable() noexcept {
    LookupTable table{};
    for (std::size_t i = 0; i < kTechnoXDescriptorValueMap.size(); ++i)
        table.values[i] = kTechnoXDescriptorValueMap[i];
    table.available = true;
    return table;
}

inline DecodeResult decodeDescriptor(const XDataMemory& memory,
                                     std::uint8_t page,
                                     std::uint8_t offset,
                                     const LookupTable& lookup) noexcept {
    DecodeResult result{};
    if (!memory.pagePresent(page)) {
        result.error = DecodeError::sourcePageMissing;
        return result;
    }
    if (static_cast<unsigned>(offset) + kDescriptorSize > kPageSize) {
        result.error = DecodeError::descriptorCrossesPage;
        return result;
    }
    if (!lookup.available) {
        result.error = DecodeError::lookupTableUnavailable;
        return result;
    }

    std::array<std::uint8_t, kDescriptorSize> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (!memory.read(page, static_cast<std::uint8_t>(offset + i), bytes[i])) {
            result.error = DecodeError::sourcePageMissing;
            return result;
        }
    }

    result.descriptor.rawFirst = bytes[0];
    result.descriptor.second = bytes[1];
    result.descriptor.mappedThird = lookup.values[bytes[2]];
    result.descriptor.fourth = bytes[3];
    result.descriptor.fifth = bytes[4];
    result.descriptor.sixth = bytes[5];
    result.descriptor.seventh = bytes[6];
    // Bytes 7..9 are skipped. Byte 10 supplies internal flags E0..E2;
    // byte 11 is translated through ROM table 0x1208, and byte 12 is copied.
    result.descriptor.controlFlags = static_cast<std::uint8_t>(bytes[10] & 0x07);
    result.descriptor.mappedEleventh = lookup.values[bytes[11]];
    result.descriptor.twelfth = bytes[12];
    result.path = bytes[1] == 0 ? LoaderPath::zeroSecondByte
                                : LoaderPath::nonzeroSecondByte;
    return result;
}

enum class LoaderAction : std::uint8_t {
    readModeControlByte,
    jumpToZeroByteHandler,
    clearSlotBusyBit,
    scanSlotStatus,
    loadReplacementFirstByte,
    continueAt1599,
    callResourceManager17D4,
    setSlotBusyBit,
    writeSecondByte,
    returnToModeControl
};

struct LoaderRuntimeFlags {
    bool bit06 = false;
    bool bit04 = false;
    bool bitE7 = false;
    bool ram52IsZero = false;
};

struct LoaderBranchPlan {
    std::array<LoaderAction, 6> actions{};
    std::size_t actionCount = 0;
    std::uint16_t slotBase = 0;
    std::uint16_t slotDataAddress = 0;
    std::uint16_t modeControlAddress = 0;
    bool valid = false;

    void append(LoaderAction action) noexcept {
        if (actionCount < actions.size()) actions[actionCount++] = action;
    }
};

struct LoaderInputs {
    std::uint8_t page = 0;
    std::uint8_t offset = 0;
    std::uint8_t mode = 0;
};

struct LoaderDecision {
    LoaderInputs input{};
    Descriptor descriptor{};
    LoaderPath path = LoaderPath::zeroSecondByte;
    // 0x1494's descriptor decode and first slot-arbitration branch are modeled;
    // indirect hardware state and external-interface writes remain explicit inputs.
    bool valid = false;
};

inline LoaderBranchPlan planLoaderBranch(const LoaderDecision& decision,
                                         LoaderRuntimeFlags flags) noexcept {
    LoaderBranchPlan plan{};
    if (!decision.valid) return plan;

    plan.valid = true;
    // The firmware computes DPTR = 0x2068 + (mode * 0x80).
    plan.slotBase = static_cast<std::uint16_t>(
        0x2068u + static_cast<unsigned>(decision.input.mode & 0x0f) * 0x80u);
    plan.slotDataAddress = static_cast<std::uint16_t>(
        plan.slotBase + (flags.bit06 ? 0u : decision.descriptor.first()));
    plan.modeControlAddress = static_cast<std::uint16_t>(
        0x28d8u + (decision.input.mode & 0x0f));

    if (decision.path == LoaderPath::zeroSecondByte) {
        if (!flags.bit06) {
            // 0x1525 reads 0x28D8 + mode, then jumps to 0x0E8E.
            plan.append(LoaderAction::readModeControlByte);
            plan.append(LoaderAction::jumpToZeroByteHandler);
            return plan;
        }

        // 0x1534..0x156E clear the busy bit, scan status, and either
        // retry the zero-byte path or load replacement descriptor bytes.
        plan.append(LoaderAction::clearSlotBusyBit);
        plan.append(LoaderAction::scanSlotStatus);
        if (flags.bitE7) {
            plan.append(LoaderAction::loadReplacementFirstByte);
            plan.append(LoaderAction::continueAt1599);
        } else {
            plan.append(LoaderAction::returnToModeControl);
        }
        return plan;
    }

    if (flags.ram52IsZero)
        plan.append(LoaderAction::callResourceManager17D4);

    if (!flags.bit06) {
        plan.append(LoaderAction::continueAt1599);
        return plan;
    }

    // 0x157C..0x1595 set bit 7 in the selected slot byte and store byte +1.
    plan.append(LoaderAction::setSlotBusyBit);
    plan.append(LoaderAction::writeSecondByte);
    if (flags.bit04)
        plan.append(LoaderAction::retryAt1544);
    else
        plan.append(LoaderAction::continueAt1599);
    return plan;
}

inline LoaderDecision prepareLoaderDecision(const XDataMemory& memory,
                                            const LookupTable& lookup,
                                            LoaderInputs input) noexcept {
    LoaderDecision decision{};
    decision.input = input;
    const auto decoded = decodeDescriptor(memory, input.page, input.offset, lookup);
    if (!decoded) return decision;
    decision.descriptor = decoded.descriptor;
    decision.path = decoded.path;
    decision.valid = true;
    return decision;
}

} // namespace technox::firmware
