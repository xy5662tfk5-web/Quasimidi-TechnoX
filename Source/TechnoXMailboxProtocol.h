#pragma once

// Conservative model of firmware routine 0x0DB7.
// It describes writes to XDATA 0x8000..0x8003 only; it does not assign
// a physical device or audio meaning to the mailbox bytes.

#include "TechnoXFirmwareTransaction.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace technox::firmware {

struct MailboxWrite {
    static constexpr std::uint16_t kBaseAddress = 0x8000;
    std::array<std::uint8_t, 4> bytes{};
    // Firmware clears internal bit 0x0C, then sets it iff outgoing byte 3
    // (R4 after ACC.6 is cleared) has bit 7 set.
    bool setInternalFlag0C = false;
};

inline MailboxWrite makeMailboxWrite(std::uint8_t r5, std::uint8_t r2,
                                     std::uint8_t r3, std::uint8_t r4) noexcept {
    MailboxWrite result{};
    result.bytes = {r5, r2, r3, static_cast<std::uint8_t>(r4 & 0xBF)};
    result.setInternalFlag0C = (result.bytes[3] & 0x80) != 0;
    return result;
}

constexpr std::uint8_t kMailboxScanPage = 0x80;
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

// Exact bounded model of firmware routine 0x1009. The list is packed:
// the first 0xFF entry terminates scanning. Every non-0xFF entry advances
// RAM[0x2B], while only entries matching (value & 0x7E) == 0x16 emit 0x0DB7.
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
                                            std::uint8_t sequence,
                                            bool flag0D) noexcept {
    MailboxScanResult result{};
    result.nextSequence = sequence;
    if (!memory.pagePresent(kMailboxScanPage)) {
        result.error = MailboxScanError::sourcePageMissing;
        return result;
    }

    for (std::uint16_t offset = kMailboxScanFirstOffset;
         offset < kMailboxScanEndOffset;
         offset += kMailboxScanStride) {
        std::uint8_t value = 0xFF;
        if (!memory.read(kMailboxScanPage, static_cast<std::uint8_t>(offset), value)) {
            result.error = MailboxScanError::sourcePageMissing;
            return result;
        }
        if (value == 0xFF) {
            result.terminatedByEmpty = true;
            break;
        }

        result.visitedOffsets[result.visitedCount++] = static_cast<std::uint8_t>(offset);
        if ((value & kMailboxEntryMarkerMask) == kMailboxEntryMarker) {
            const auto r4 = static_cast<std::uint8_t>(flag0D ? 0x80 : 0x00);
            result.writes[result.writeCount++] = makeMailboxWrite(
                result.nextSequence, 0x80, 0x01, r4);
        }
        result.nextSequence = static_cast<std::uint8_t>(result.nextSequence + 1);
    }
    return result;
}

} // namespace technox::firmware
