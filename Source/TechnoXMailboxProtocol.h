#pragma once

// Conservative model of firmware routines 0x0DB7, 0x0DCE and 0x1009.
// XDATA mailbox writes are modeled without assigning a physical device or
// audio meaning to addresses 0x8000..0x8003.

#include "TechnoXFirmwareTransaction.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace technox::firmware {

struct MailboxWrite {
    static constexpr std::uint16_t kBaseAddress = 0x8000;
    std::array<std::uint8_t, 4> bytes{};
    // 0x0DB7 clears ACC.6 before writing R4, then sets internal bit 0x0C
    // iff the separate processor flag E7 is set. E7 is not derived from R4.
    bool setInternalFlag0C = false;
};

inline MailboxWrite makeMailboxWrite(std::uint8_t r5, std::uint8_t r2,
                                     std::uint8_t r3, std::uint8_t r4,
                                     bool flagE7) noexcept {
    MailboxWrite result{};
    result.bytes = {r5, r2, r3, static_cast<std::uint8_t>(r4 & 0xBF)};
    result.setInternalFlag0C = flagE7;
    return result;
}

// Exact state transition from 0x0DCE. E7 is compared with internal bit 0x0C.
// If they differ, the flag is synchronized and XDATA 0x8003 receives 0xC0
// when setting or 0x40 when clearing. If equal, that byte is not written.
struct MailboxHandshakeTransition {
    bool nextFlag0C = false;
    bool writesFourthByte = false;
    std::uint8_t fourthByte = 0;
};

inline MailboxHandshakeTransition transitionMailboxHandshake(
    bool flag0C, bool flagE7) noexcept {
    MailboxHandshakeTransition result{};
    result.nextFlag0C = flag0C;
    if (flagE7 == flag0C)
        return result;

    result.nextFlag0C = flagE7;
    result.writesFourthByte = true;
    result.fourthByte = flagE7 ? 0xC0 : 0x40;
    return result;
}

constexpr std::uint8_t kMailboxScanFirstOffset = 0x80;
constexpr std::uint8_t kMailboxScanEndOffset = 0xF8;
constexpr std::uint8_t kMailboxScanStride = 0x08;
constexpr std::size_t kMailboxScanCapacity = 15;
constexpr std::uint8_t kMailboxEntryMarker = 0x16;
constexpr std::uint8_t kMailboxEntryMarkerMask = 0x7E;

enum class MailboxScanError : std::uint8_t {
    none,
    sourcePageMissing
};

// 0x1009 selects its XDATA page from RAM[0x2D], scans offsets 0x80..0xF0
// in steps of eight, stops at 0xFF, and advances RAM[0x2B] for each occupied
// entry. Entries matching (value & 0x7E) == 0x16 call 0x0DB7 with R4=0;
// E7 is independently set iff the routine's flag 0x0D is set.
struct MailboxScanResult {
    std::array<std::uint8_t, kMailboxScanCapacity> visitedOffsets{};
    std::array<MailboxWrite, kMailboxScanCapacity> writes{};
    std::size_t visitedCount = 0;
    std::size_t writeCount = 0;
    std::uint8_t nextSequence = 0;
    bool terminatedByEmpty = false;
    MailboxScanError error = MailboxScanError::none;

    explicit operator bool() const noexcept { return error == MailboxScanError::none; }
};

inline MailboxScanResult scanMailboxEntries(const XDataMemory& memory,
                                            std::uint8_t sourcePage,
                                            std::uint8_t sequence,
                                            bool flag0D) noexcept {
    MailboxScanResult result{};
    result.nextSequence = sequence;
    if (!memory.pagePresent(sourcePage)) {
        result.error = MailboxScanError::sourcePageMissing;
        return result;
    }

    for (std::uint16_t offset = kMailboxScanFirstOffset;
         offset < kMailboxScanEndOffset;
         offset += kMailboxScanStride) {
        std::uint8_t value = 0xFF;
        if (!memory.read(sourcePage, static_cast<std::uint8_t>(offset), value)) {
            result.error = MailboxScanError::sourcePageMissing;
            return result;
        }
        if (value == 0xFF) {
            result.terminatedByEmpty = true;
            break;
        }

        result.visitedOffsets[result.visitedCount++] = static_cast<std::uint8_t>(offset);
        if ((value & kMailboxEntryMarkerMask) == kMailboxEntryMarker) {
            result.writes[result.writeCount++] = makeMailboxWrite(
                result.nextSequence, 0x80, 0x01, 0x00, flag0D);
        }
        result.nextSequence = static_cast<std::uint8_t>(result.nextSequence + 1);
    }
    return result;
}

} // namespace technox::firmware
