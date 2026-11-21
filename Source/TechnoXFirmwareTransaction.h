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
constexpr std::size_t kDescriptorSize = 12;

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
    // bytes[7] and bytes[8] are not copied into the loader's byte variables.
    result.descriptor.controlFlags = static_cast<std::uint8_t>(bytes[9] & 0x07);
    result.descriptor.mappedEleventh = lookup.values[bytes[10]];
    result.descriptor.twelfth = bytes[11];
    result.path = bytes[1] == 0 ? LoaderPath::zeroSecondByte
                                : LoaderPath::nonzeroSecondByte;
    return result;
}

struct LoaderInputs {
    std::uint8_t page = 0;
    std::uint8_t offset = 0;
    std::uint8_t mode = 0;
};

struct LoaderDecision {
    LoaderInputs input{};
    Descriptor descriptor{};
    LoaderPath path = LoaderPath::zeroSecondByte;
    // 0x1494's first branch is classified exactly. Subsequent slot arbitration
    // and external-interface writes are intentionally not guessed here.
    bool valid = false;
};

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
