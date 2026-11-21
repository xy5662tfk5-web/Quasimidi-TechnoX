#pragma once

// Conservative model of firmware routine 0x0DB7.
// It describes writes to XDATA 0x8000..0x8003 only; it does not assign
// a physical device or audio meaning to the mailbox bytes.

#include <array>
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

} // namespace technox::firmware
