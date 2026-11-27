#pragma once

#include <cstdint>

namespace technox {

// Address arithmetic recovered from IC4 v1.05 handlers 0xAC00 (Note On)
// and 0xAC88 (Note Off). This describes the firmware's byte/bit selection
// only; it does not identify the physical device or the meaning of its pages.
struct MASSNoteEventAddress {
    std::uint8_t eventId = 0;
    std::uint8_t note = 0;
    std::uint8_t byteOffset = 0;
    std::uint8_t bitIndex = 0;
    std::uint8_t noteOnDispatchOffset = 0;
    std::uint8_t noteOffDispatchOffset = 0;
};

struct MASSNoteEventAddressing {
    static constexpr std::uint8_t kEventCount = 16;
    static constexpr std::uint8_t kNotesPerEvent = 128;
    static constexpr std::uint8_t kBytesPerEvent = 16;
    static constexpr std::uint8_t kBitsPerByte = 8;
    static constexpr std::uint8_t kNoteOnBranchStride = 14;
    static constexpr std::uint8_t kNoteOffBranchStride = 17;

    // Firmware arithmetic:
    //   R0 = eventId * 0x10 + (note / 8)
    //   Note On jump = 0xAC17 + 14 * (note % 8)
    //   Note Off jump = 0xACA1 + 17 * (note % 8)
    static constexpr bool decode(std::uint8_t eventId,
                                 std::uint8_t note,
                                 MASSNoteEventAddress& result) noexcept {
        if (eventId >= kEventCount || note >= kNotesPerEvent)
            return false;

        const auto byteWithinEvent =
            static_cast<std::uint8_t>(note / kBitsPerByte);
        const auto bitWithinByte =
            static_cast<std::uint8_t>(note % kBitsPerByte);
        result.eventId = eventId;
        result.note = note;
        result.byteOffset = static_cast<std::uint8_t>(
            eventId * kBytesPerEvent + byteWithinEvent);
        result.bitIndex = bitWithinByte;
        result.noteOnDispatchOffset = static_cast<std::uint8_t>(
            bitWithinByte * kNoteOnBranchStride);
        result.noteOffDispatchOffset = static_cast<std::uint8_t>(
            bitWithinByte * kNoteOffBranchStride);
        return true;
    }
};

} // namespace technox
