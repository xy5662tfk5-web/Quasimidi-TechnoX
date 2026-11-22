#pragma once

// Decoder for the IC51 resource directory and M.A.S.S. command streams.
// This models the firmware-visible ROM structure only; it does not decode
// PCM samples or emulate the external sound processor.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace technox {

enum class MASSResourceBank : std::uint8_t {
    bank0 = 0,
    bank1 = 1
};

enum class MASSResourceCommandKind : std::uint8_t {
    singleByte,
    pitchParameter,
    multiParameter,
    emptyMultiParameter,
    fourByteCommand,
    threePayloadCommand,
    marker
};

struct MASSResourceSelection {
    MASSResourceBank bank = MASSResourceBank::bank0;
    std::uint8_t pointerIndex = 0;
    std::uint8_t p3BankMask = 0;
    std::uint8_t normalizedPartParameter = 0;
};

struct MASSResourceCommand {
    std::uint16_t bankOffset = 0;
    std::uint8_t opcode = 0;
    MASSResourceCommandKind kind = MASSResourceCommandKind::singleByte;
    std::uint8_t length = 0;
    std::array<std::uint8_t, 8> bytes{};
};

struct MASSDecodedResourceStream {
    MASSResourceBank bank = MASSResourceBank::bank0;
    std::uint8_t resourceIndex = 0;
    std::uint16_t startPointer = 0;
    std::uint16_t endOffset = 0;
    std::uint16_t bytesConsumed = 0;
    std::uint8_t slotsConsumed = 0;
    bool terminated = false;
    bool truncated = false;
    bool slotLimitReached = false;
    std::vector<MASSResourceCommand> commands;
};

class MASSResourceROM {
public:
    static constexpr std::size_t kBankSize = 0x10000;
    static constexpr std::size_t kBankCount = 2;
    static constexpr std::size_t kImageSize = kBankSize * kBankCount;
    static constexpr std::size_t kPointerCount = 256;
    static constexpr std::size_t kPointerTableSize = kPointerCount * 2;
    static constexpr std::size_t kMaximumSlots = 16;

    bool load(const std::uint8_t* bytes, std::size_t size) noexcept {
        if (bytes == nullptr || size != kImageSize)
            return false;
        std::copy_n(bytes, kImageSize, image_.begin());
        loaded_ = true;
        return true;
    }

    void clear() noexcept {
        image_.fill(0xff);
        loaded_ = false;
    }

    bool isLoaded() const noexcept { return loaded_; }

    // Normal Part path reconstructed from IC4 0x418E:
    // parameter 0/1 selects P3 mask 0x07 and bank 0;
    // parameter 2/3 selects P3 mask 0x06 and bank 1.
    // Bit 0 chooses the lower/upper 128-entry half of the 256-entry table.
    // Firmware clamps values above 3 to 3. The special runtime Part index 9
    // follows a separate path and is not represented by this helper.
    static bool resolvePartResource(int partParameter,
                                    std::uint8_t resourceSubIndex,
                                    MASSResourceSelection& selection) noexcept {
        if (partParameter < 0)
            return false;

        const auto normalized = static_cast<std::uint8_t>(
            partParameter > 3 ? 3 : partParameter);
        selection.normalizedPartParameter = normalized;
        selection.bank = normalized < 2 ? MASSResourceBank::bank0
                                        : MASSResourceBank::bank1;
        selection.p3BankMask = normalized < 2 ? 0x07 : 0x06;
        selection.pointerIndex = static_cast<std::uint8_t>(
            resourceSubIndex + ((normalized & 0x01) != 0 ? 0x80 : 0x00));
        return true;
    }

    // The firmware reads the first pointer byte into R6, the second into R7,
    // then places R6 at descriptor +5 and R7 at +6. The resulting DPTR is
    // therefore a little-endian 16-bit pointer in each 64KB bank.
    bool readPointer(MASSResourceBank bank,
                     std::uint8_t resourceIndex,
                     std::uint16_t& pointer) const noexcept {
        if (!loaded_ || !validBank(bank))
            return false;

        const auto address = bankBase(bank) +
            static_cast<std::size_t>(resourceIndex) * 2;
        const auto low = image_[address];
        const auto high = image_[address + 1];
        pointer = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(high) << 8 | low);
        return true;
    }

    // Parses at most 16 command slots, matching the firmware's RAM[0x2C]
    // counter. Command payload bytes are retained verbatim; parameter
    // semantics are deliberately not guessed.
    bool decodeResource(MASSResourceBank bank,
                        std::uint8_t resourceIndex,
                        MASSDecodedResourceStream& result) const {
        std::uint16_t pointer = 0;
        if (!readPointer(bank, resourceIndex, pointer))
            return false;

        MASSDecodedResourceStream decoded{};
        decoded.bank = bank;
        decoded.resourceIndex = resourceIndex;
        decoded.startPointer = pointer;

        const auto base = bankBase(bank);
        const auto bankEnd = base + kBankSize;
        auto cursor = base + pointer;

        while (decoded.slotsConsumed < kMaximumSlots) {
            if (cursor >= bankEnd) {
                decoded.truncated = true;
                break;
            }

            const auto opcode = image_[cursor];
            if (opcode == 0xff) {
                decoded.terminated = true;
                break;
            }

            const auto group = static_cast<std::uint8_t>(opcode & 0x38);
            MASSResourceCommand command{};
            command.bankOffset = static_cast<std::uint16_t>(cursor - base);
            command.opcode = opcode;
            command.bytes[0] = opcode;
            command.length = 1;

            if (group == 0x00) {
                command.kind = MASSResourceCommandKind::singleByte;
            } else if (group == 0x08) {
                command.kind = MASSResourceCommandKind::pitchParameter;
                command.length = 7;
            } else if (group == 0x10) {
                if (cursor + 1 >= bankEnd) {
                    decoded.truncated = true;
                    break;
                }
                if (image_[cursor + 1] == 0) {
                    // Firmware 0x1EDD skips eight bytes when byte 1 is zero.
                    command.kind = MASSResourceCommandKind::emptyMultiParameter;
                    command.length = 8;
                } else {
                    command.kind = MASSResourceCommandKind::multiParameter;
                    command.length = 7;
                }
            } else if (group == 0x18) {
                command.kind = MASSResourceCommandKind::fourByteCommand;
                command.length = 4;
            } else if (group == 0x20) {
                command.kind = MASSResourceCommandKind::threePayloadCommand;
                command.length = 4;
            } else if (group == 0x38) {
                // 0x38..0x3F are handled by the interpreter's marker path,
                // not by the 0x1BC3 jump table.
                command.kind = MASSResourceCommandKind::marker;
            } else {
                // Groups 0x28 and 0x30 share handler 0x2374 (advance by one).
                command.kind = MASSResourceCommandKind::singleByte;
            }

            if (cursor + command.length > bankEnd) {
                decoded.truncated = true;
                break;
            }

            std::copy_n(image_.begin() + static_cast<std::ptrdiff_t>(cursor),
                        command.length, command.bytes.begin());
            decoded.commands.push_back(command);
            cursor += command.length;
            ++decoded.slotsConsumed;
        }

        decoded.endOffset = static_cast<std::uint16_t>(cursor - base);
        decoded.bytesConsumed = static_cast<std::uint16_t>(cursor - (base + pointer));
        decoded.slotLimitReached =
            decoded.slotsConsumed == kMaximumSlots &&
            !decoded.terminated && !decoded.truncated;
        result = std::move(decoded);
        return true;
    }

private:
    static bool validBank(MASSResourceBank bank) noexcept {
        return bank == MASSResourceBank::bank0 || bank == MASSResourceBank::bank1;
    }

    static constexpr std::size_t bankBase(MASSResourceBank bank) noexcept {
        return bank == MASSResourceBank::bank0 ? 0 : kBankSize;
    }

    std::array<std::uint8_t, kImageSize> image_{};
    bool loaded_ = false;
};

} // namespace technox
