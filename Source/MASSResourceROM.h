#pragma once

// IC51 resource directory, external descriptor and M.A.S.S. stream decoders.
// These classes model firmware-visible data structures only. They do not
// decode PCM samples or emulate the external sound processor.

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

struct MASSResourceSelection {
    MASSResourceBank bank = MASSResourceBank::bank0;
    std::uint8_t pointerIndex = 0;
    std::uint8_t p3BankMask = 0;
    std::uint8_t normalizedPartParameter = 0;
};

class MASSResourceROM {
public:
    static constexpr std::size_t kBankSize = 0x10000;
    static constexpr std::size_t kBankCount = 2;
    static constexpr std::size_t kImageSize = kBankSize * kBankCount;
    static constexpr std::size_t kPointerCount = 256;
    static constexpr std::size_t kPointerTableSize = kPointerCount * 2;

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
    // parameter 0/1 selects P3 mask 0x07 and ROM bank 0;
    // parameter 2/3 selects P3 mask 0x06 and ROM bank 1.
    // Bit 0 selects the lower/upper 128-entry half of the pointer table.
    // Firmware clamps values above 3 to 3. Runtime Part index 9 has a
    // separate firmware path and is not represented by this helper.
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

    // IC51 pointer-table entries are big-endian descriptor addresses.
    // The firmware reads byte 0 into R6 and byte 1 into R7, then 0x1494
    // forms the descriptor address with R6 as the high byte.
    bool readResourceDescriptorAddress(MASSResourceBank bank,
                                       std::uint8_t resourceIndex,
                                       std::uint16_t& address) const noexcept {
        if (!loaded_ || !validBank(bank))
            return false;

        const auto offset = bankBase(bank) +
            static_cast<std::size_t>(resourceIndex) * 2;
        const auto high = image_[offset];
        const auto low = image_[offset + 1];
        address = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(high) << 8 | low);
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

struct MASSResourceDescriptorEntry {
    std::uint8_t selector = 0;
    std::uint8_t data0 = 0;
    std::uint8_t data1 = 0;
};

struct MASSResourceDescriptorGroup {
    std::uint8_t groupId = 0;
    std::vector<MASSResourceDescriptorEntry> entries;
};

struct MASSResourceDescriptorTableEntry {
    std::uint8_t key = 0;
    std::uint8_t data0 = 0;
    std::uint8_t data1 = 0;
};

struct MASSResourceDescriptor {
    std::uint16_t descriptorAddress = 0;
    std::array<std::uint8_t, 8> fixedHeader{};
    std::vector<MASSResourceDescriptorGroup> tableA;
    std::vector<MASSResourceDescriptorTableEntry> tableB;
    std::uint8_t tableATerminator = 0;
    std::uint8_t tableBTerminator = 0;
    std::uint16_t commandStreamPointer = 0;
    std::size_t headerEndOffset = 0;
};

class MASSResourceDescriptorParser {
public:
    // Parses the descriptor memory bank selected by the firmware's separate
    // P3 flags (descriptor byte +10). This bank is not assumed to be IC51.
    // At descriptor +8 the firmware scans:
    //   Table A: group IDs 0..4; selector + two data bytes; selector 0x7F
    //            terminates a group; group ID >=5 terminates Table A.
    //   Table B: keys 0..5 plus two data bytes; key >=6 terminates Table B.
    // The following two bytes are the little-endian command-stream pointer.
    static bool parse(const std::uint8_t* bankData,
                      std::size_t bankSize,
                      std::uint16_t descriptorAddress,
                      MASSResourceDescriptor& result) {
        if (bankData == nullptr ||
            static_cast<std::size_t>(descriptorAddress) + 8 > bankSize)
            return false;

        MASSResourceDescriptor parsed{};
        parsed.descriptorAddress = descriptorAddress;
        std::copy_n(bankData + descriptorAddress, 8, parsed.fixedHeader.begin());
        std::size_t cursor = static_cast<std::size_t>(descriptorAddress) + 8;

        bool tableAEnded = false;
        for (std::size_t groupNumber = 0; groupNumber < 6; ++groupNumber) {
            if (cursor >= bankSize)
                return false;
            const auto groupId = bankData[cursor++];
            if (groupId >= 5) {
                parsed.tableATerminator = groupId;
                tableAEnded = true;
                break;
            }

            MASSResourceDescriptorGroup group{};
            group.groupId = groupId;
            bool groupEnded = false;
            for (std::size_t entryNumber = 0; entryNumber < 256; ++entryNumber) {
                if (cursor + 2 >= bankSize)
                    return false;
                MASSResourceDescriptorEntry entry{};
                entry.selector = bankData[cursor++];
                entry.data0 = bankData[cursor++];
                entry.data1 = bankData[cursor++];
                group.entries.push_back(entry);
                if (entry.selector == 0x7f) {
                    groupEnded = true;
                    break;
                }
            }
            if (!groupEnded)
                return false;
            parsed.tableA.push_back(std::move(group));
        }
        if (!tableAEnded)
            return false;

        bool tableBEnded = false;
        for (std::size_t entryNumber = 0; entryNumber < 256; ++entryNumber) {
            if (cursor >= bankSize)
                return false;
            const auto key = bankData[cursor++];
            if (key >= 6) {
                parsed.tableBTerminator = key;
                tableBEnded = true;
                break;
            }
            if (cursor + 1 >= bankSize)
                return false;
            MASSResourceDescriptorTableEntry entry{};
            entry.key = key;
            entry.data0 = bankData[cursor++];
            entry.data1 = bankData[cursor++];
            parsed.tableB.push_back(entry);
        }
        if (!tableBEnded || cursor + 1 >= bankSize)
            return false;

        const auto low = bankData[cursor++];
        const auto high = bankData[cursor++];
        parsed.commandStreamPointer = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(high) << 8 | low);
        parsed.headerEndOffset = cursor;
        result = std::move(parsed);
        return true;
    }
};

enum class MASSResourceCommandKind : std::uint8_t {
    conditionalRecord,
    pitchParameter,
    multiParameter,
    emptyMultiParameter,
    fourByteCommand,
    threePayloadCommand,
    marker,
    singleByteAdvance
};

struct MASSResourceCommand {
    std::uint16_t bankOffset = 0;
    std::uint8_t opcode = 0;
    MASSResourceCommandKind kind = MASSResourceCommandKind::singleByte;
    std::uint8_t length = 0;
    bool routesToHardware = false;
    std::array<std::uint8_t, 8> bytes{};
};

struct MASSDecodedResourceStream {
    std::uint16_t streamPointer = 0;
    std::size_t endOffset = 0;
    std::size_t bytesConsumed = 0;
    std::uint8_t slotsConsumed = 0;
    bool terminated = false;
    bool truncated = false;
    bool slotLimitReached = false;
    std::vector<MASSResourceCommand> commands;
};

class MASSCommandStreamDecoder {
public:
    static constexpr std::size_t kMaximumSlots = 16;

    // Decodes a stream from the memory bank selected by the descriptor's
    // P3 flags. The bank is supplied separately; no assumption is made that
    // the stream resides in the IC51 pointer-table bank.
    // firmwareFlag0ASet is the live firmware flag 0x0A which changes how
    // opcode group 0x00 is interpreted. With it clear, byte +3 bit 3 can
    // select the longer path through handler 0x224C.
    static bool decode(const std::uint8_t* bankData,
                       std::size_t bankSize,
                       std::uint16_t streamPointer,
                       MASSDecodedResourceStream& result,
                       bool firmwareFlag0ASet = false) {
        if (bankData == nullptr || static_cast<std::size_t>(streamPointer) >= bankSize)
            return false;

        MASSDecodedResourceStream decoded{};
        decoded.streamPointer = streamPointer;
        std::size_t cursor = streamPointer;

        while (decoded.slotsConsumed < kMaximumSlots) {
            if (cursor >= bankSize) {
                decoded.truncated = true;
                break;
            }

            const auto opcode = bankData[cursor];
            if (opcode == 0xff) {
                decoded.terminated = true;
                break;
            }

            const auto group = static_cast<std::uint8_t>(opcode & 0x38);
            MASSResourceCommand command{};
            command.bankOffset = static_cast<std::uint16_t>(cursor);
            command.opcode = opcode;
            command.bytes[0] = opcode;
            command.length = 1;

            if (group == 0x00) {
                // Handler 0x1C2B: normally checks byte +3 bit 3. If clear,
                // the record consumes four stream bytes; if set, execution
                // continues at byte +3 in handler 0x224C and consumes seven.
                // Firmware flag 0x0A instead selects the four-byte 0x1BEF path.
                command.kind = MASSResourceCommandKind::conditionalRecord;
                command.length = 4;
                if (!firmwareFlag0ASet) {
                    if (cursor + 3 >= bankSize) {
                        decoded.truncated = true;
                        break;
                    }
                    if ((bankData[cursor + 3] & 0x08) != 0) {
                        command.length = 7;
                        command.routesToHardware = true;
                    }
                }
            } else if (group == 0x08) {
                command.kind = MASSResourceCommandKind::pitchParameter;
                command.length = 7;
            } else if (group == 0x10) {
                if (cursor + 1 >= bankSize) {
                    decoded.truncated = true;
                    break;
                }
                if (bankData[cursor + 1] == 0) {
                    // The zero subcase advances the stream pointer by seven
                    // and writes a 0x38 filler record; it is not eight bytes.
                    command.kind = MASSResourceCommandKind::emptyMultiParameter;
                    command.length = 7;
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
                // The interpreter catches 0x38 before the jump table and
                // writes a filler slot, then advances the stream by one.
                command.kind = MASSResourceCommandKind::marker;
            } else {
                // Groups 0x28 and 0x30 share handler 0x2374: one-byte advance.
                command.kind = MASSResourceCommandKind::singleByteAdvance;
            }

            if (cursor + command.length > bankSize) {
                decoded.truncated = true;
                break;
            }

            std::copy_n(bankData + cursor, command.length, command.bytes.begin());
            decoded.commands.push_back(command);
            cursor += command.length;
            ++decoded.slotsConsumed;
        }

        decoded.endOffset = cursor;
        decoded.bytesConsumed = cursor - streamPointer;
        decoded.slotLimitReached =
            decoded.slotsConsumed == kMaximumSlots &&
            !decoded.terminated && !decoded.truncated;
        result = std::move(decoded);
        return true;
    }
};

} // namespace technox
