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

// Fixed IC4 v1.05 lookup tables reached by MOVC instructions.
enum class MASSFirmwareLookupTable : std::uint8_t {
    modulationScale11E0, modulationScale11E8,
    interpolationCurve11F0, interpolationCurve11FC,
    parameterMap1208, nonlinearScale1288, quantizeMap1308
};

class MASSFirmwareROMView {
public:
    static constexpr std::size_t kMinimumImageSize = 0x1408;
    bool attach(const std::uint8_t* image, std::size_t imageSize) noexcept {
        if (image == nullptr || imageSize < kMinimumImageSize) return false;
        image_ = image; imageSize_ = imageSize; return true;
    }
    void detach() noexcept { image_ = nullptr; imageSize_ = 0; }
    bool isAttached() const noexcept { return image_ != nullptr; }

    static constexpr std::uint16_t tableAddress(MASSFirmwareLookupTable table) noexcept {
        switch (table) {
            case MASSFirmwareLookupTable::modulationScale11E0: return 0x11e0;
            case MASSFirmwareLookupTable::modulationScale11E8: return 0x11e8;
            case MASSFirmwareLookupTable::interpolationCurve11F0: return 0x11f0;
            case MASSFirmwareLookupTable::interpolationCurve11FC: return 0x11fc;
            case MASSFirmwareLookupTable::parameterMap1208: return 0x1208;
            case MASSFirmwareLookupTable::nonlinearScale1288: return 0x1288;
            case MASSFirmwareLookupTable::quantizeMap1308: return 0x1308;
        }
        return 0;
    }
    static constexpr std::size_t tableLength(MASSFirmwareLookupTable table) noexcept {
        switch (table) {
            case MASSFirmwareLookupTable::modulationScale11E0: return 8;
            case MASSFirmwareLookupTable::modulationScale11E8: return 8;
            case MASSFirmwareLookupTable::interpolationCurve11F0: return 12;
            case MASSFirmwareLookupTable::interpolationCurve11FC: return 12;
            case MASSFirmwareLookupTable::parameterMap1208: return 128;
            case MASSFirmwareLookupTable::nonlinearScale1288: return 128;
            case MASSFirmwareLookupTable::quantizeMap1308: return 256;
        }
        return 0;
    }
    bool read(MASSFirmwareLookupTable table, std::size_t index,
              std::uint8_t& value) const noexcept {
        if (image_ == nullptr || index >= tableLength(table)) return false;
        const auto offset = static_cast<std::size_t>(tableAddress(table)) + index;
        if (offset >= imageSize_) return false;
        value = image_[offset]; return true;
    }
    // The two MOVC lookups in the common 0x1EDD path; caller supplies the
    // 7-bit index after the firmware's preceding rotate/complement operations.
    bool lookupNonlinearScale(std::uint8_t index, std::uint8_t& value) const noexcept {
        std::uint8_t intermediate = 0;
        return read(MASSFirmwareLookupTable::nonlinearScale1288, index, intermediate) &&
               read(MASSFirmwareLookupTable::quantizeMap1308, intermediate, value);
    }
private:
    const std::uint8_t* image_ = nullptr;
    std::size_t imageSize_ = 0;
};

struct MASS2381AddressResult {
    std::uint16_t address = 0;
    std::uint8_t selector = 0;
    std::uint8_t offset = 0;
};

// Reconstructs the DPTR calculation in IC4 helper 0x2381. The returned
// address is a bus address only; caller still supplies the P3-selected memory.
class MASS2381AddressBuilder {
public:
    static MASS2381AddressResult resolve(std::uint16_t basePointer,
                                         std::uint8_t selectorInputA,
                                         std::uint8_t ram2F) noexcept {
        // 0x2381 receives the already-normalized selector in A. Callers
        // prepare it differently: e.g. SWAP R5; ANL A,#3 or MOVX A; ANL A,#3.
        const auto selector = static_cast<std::uint8_t>(selectorInputA & 0x03u);
        const auto adjusted = static_cast<std::uint8_t>(ram2F + 0xf4u);
        std::uint8_t offset = 0;

        if ((adjusted & 0x80u) == 0) {
            const auto half = static_cast<std::uint8_t>(adjusted >> 1);
            const auto threshold = static_cast<std::uint8_t>(half + 0xd0u);
            offset = (threshold & 0x80u) != 0 ? half : 0x2f;
        }

        const auto address = static_cast<std::uint16_t>(
            basePointer + static_cast<unsigned>(selector) * 0x30u + offset);
        return {address, selector, offset};
    }
};

struct MASSModulationScaleResult {
    std::uint8_t value = 0;
    std::uint8_t combinedCoefficient = 0;
};

// Table-driven 0x23C7 branch for slot-byte mask 0x41 only. Other opcode
// masks enter the alternate 0x1890 paths and are deliberately not guessed.
class MASSModulationScaleMode41 {
public:
    static bool apply(const MASSFirmwareROMView& rom,
                      std::uint8_t slotOpcode,
                      std::uint8_t ram41,
                      std::uint8_t ram43,
                      std::uint8_t r2,
                      MASSModulationScaleResult& result) noexcept {
        if ((slotOpcode & 0x41u) != 0x41u)
            return false;

        std::uint8_t b = 0xff;
        std::uint8_t coefficient = 0xff;
        std::uint8_t lookup = 0;

        if (ram41 != 0 && (ram41 & 0x80u) != 0) {
            const auto index = static_cast<std::uint8_t>((ram41 >> 4) & 0x07u);
            if (!rom.read(MASSFirmwareLookupTable::modulationScale11E0,
                          index, coefficient))
                return false;
            b = coefficient;
        }

        if (ram43 != 0 && (ram43 & 0x80u) != 0) {
            const auto index = static_cast<std::uint8_t>((ram43 >> 4) & 0x07u);
            if (!rom.read(MASSFirmwareLookupTable::modulationScale11E8,
                          index, lookup))
                return false;
            const auto product = static_cast<std::uint16_t>(lookup) * b;
            b = static_cast<std::uint8_t>(product >> 8);
        }

        const auto finalProduct = static_cast<std::uint16_t>(r2) * b;
        result = {
            static_cast<std::uint8_t>(finalProduct >> 8),
            b
        };
        return true;
    }
};

struct MASS1848Result {
    std::uint8_t value = 0;
    bool carryOut = false;
    bool overflow = false; // PSW.2 (bit address 0xD2) after ADD A,R2
    bool saturated = false;
};

// Instruction-level model of IC4 subroutine 0x1848. 0xE7 is ACC.7 and is
// re-tested after each XCH; 0xD2 is PSW.2/OV, not an external mode flag.
class MASS1848Arithmetic {
public:
    static MASS1848Result execute(std::uint8_t accumulator,
                                  std::uint8_t bRegister,
                                  std::uint8_t r2) noexcept {
        bool tempBit3 = false; // bit-address 0x03 in bit-addressable IRAM

        if ((accumulator & 0x80u) != 0) {
            tempBit3 = true;
            accumulator = negate(accumulator);
        }
        swap(accumulator, bRegister);
        if ((accumulator & 0x80u) != 0) {
            tempBit3 = !tempBit3;
            accumulator = negate(accumulator);
        }

        accumulator = static_cast<std::uint8_t>(rotateLeft(accumulator) | 0x01u);
        multiplyAndExchange(accumulator, bRegister);
        if (tempBit3)
            accumulator = negate(accumulator);

        bRegister = r2;
        tempBit3 = false;
        if ((accumulator & 0x80u) != 0) {
            tempBit3 = true;
            accumulator = negate(accumulator);
        }
        swap(accumulator, bRegister);
        if ((accumulator & 0x80u) != 0) {
            tempBit3 = !tempBit3;
            accumulator = negate(accumulator);
        }

        accumulator = static_cast<std::uint8_t>(rotateLeft(accumulator) | 0x01u);
        multiplyAndExchange(accumulator, bRegister);
        if (tempBit3)
            accumulator = negate(accumulator);

        const auto sum = static_cast<unsigned>(accumulator) + r2;
        const bool carry = sum > 0xffu;
        const auto raw = static_cast<std::uint8_t>(sum & 0xffu);
        const bool overflow =
            ((static_cast<std::uint8_t>(~(accumulator ^ r2)) &
              static_cast<std::uint8_t>(accumulator ^ raw) & 0x80u) != 0);

        if (overflow)
            return {static_cast<std::uint8_t>(carry ? 0x81 : 0x7f),
                    carry, true, true};
        return {raw, carry, false, false};
    }

private:
    static constexpr std::uint8_t negate(std::uint8_t value) noexcept {
        return static_cast<std::uint8_t>(0u - value);
    }
    static constexpr std::uint8_t rotateLeft(std::uint8_t value) noexcept {
        return static_cast<std::uint8_t>((value << 1) | (value >> 7));
    }
    static void swap(std::uint8_t& a, std::uint8_t& b) noexcept {
        const auto temp = a; a = b; b = temp;
    }
    static void multiplyAndExchange(std::uint8_t& a, std::uint8_t& b) noexcept {
        const auto product = static_cast<std::uint16_t>(a) * b;
        a = static_cast<std::uint8_t>(product & 0xffu); // MUL AB: low byte in A
        b = static_cast<std::uint8_t>(product >> 8);    // high byte in B
        swap(a, b);                                     // XCH A,F0
    }
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

// Firmware XDATA addressing for MOVX @R0: P2 selects the page and R0 is
// the eight-bit offset. Incrementing R0 wraps within the selected page.
struct MASSXDataAddress {
    std::uint8_t page = 0;
    std::uint8_t offset = 0;

    static constexpr MASSXDataAddress fromLinear(std::uint16_t address) noexcept {
        return {static_cast<std::uint8_t>(address >> 8),
                static_cast<std::uint8_t>(address & 0xff)};
    }
    constexpr std::uint16_t linearAddress() const noexcept {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(page) << 8 | offset);
    }
};

// Twelve-byte per-Part resource-selection record at XDATA 0x2900 + 12*Part.
// 0x4256 stores its absolute address in 0x2B98/0x2B99; callers restore the
// high byte to P2 and low byte to R0 before 0x1494 reads it.
struct MASSResourceSelectionRecord {
    MASSXDataAddress xdataAddress{};
    std::array<std::uint8_t, 12> bytes{};

    std::uint8_t selectionCode() const noexcept {
        return static_cast<std::uint8_t>(bytes[0] & 0x7f);
    }

    // 0x1494 loads byte +5 into RAM[0x37] (DPH) and byte +6 into
    // RAM[0x36] (DPL), then 0x15AE begins scanning at address +8.
    std::uint16_t descriptorAddress() const noexcept {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[5]) << 8 | bytes[6]);
    }

    // 0x14E8 reads byte +9 into A and copies ACC.0..ACC.2 into
    // internal flags 0..2; those flags drive the active-low P3 controls.
    std::uint8_t p3ControlFlags() const noexcept {
        return static_cast<std::uint8_t>(bytes[9] & 0x07);
    }

    // These two input bytes are separately translated through code-ROM
    // lookup table 0x1208 by the firmware; expose indices, not guessed names.
    std::uint8_t lookupIndex0() const noexcept { return bytes[2]; }
    std::uint8_t lookupIndex1() const noexcept { return bytes[10]; }
};

class MASSResourceSelectionRecordParser {
public:
    static MASSXDataAddress addressForRuntimePart(std::uint8_t runtimePartIndex) noexcept {
        const auto address = static_cast<std::uint16_t>(
            0x2900u + 12u * static_cast<unsigned>(runtimePartIndex));
        return MASSXDataAddress::fromLinear(address);
    }

    // pageWindow is the 256-byte window already selected by P2. Reads wrap
    // the R0 offset exactly as MOVX @R0 + INC R0 does on the 8051.
    static bool parseSelectedPage(const std::uint8_t* pageWindow,
                                  std::size_t pageSize,
                                  MASSXDataAddress address,
                                  MASSResourceSelectionRecord& result) noexcept {
        if (pageWindow == nullptr || pageSize < 256)
            return false;
        MASSResourceSelectionRecord parsed{};
        parsed.xdataAddress = address;
        for (std::size_t i = 0; i < parsed.bytes.size(); ++i) {
            const auto offset = static_cast<std::uint8_t>(
                static_cast<unsigned>(address.offset) + static_cast<unsigned>(i));
            parsed.bytes[i] = pageWindow[offset];
        }
        result = parsed;
        return true;
    }

    static bool parse(const std::uint8_t* bankData,
                      std::size_t bankSize,
                      std::size_t recordOffset,
                      MASSResourceSelectionRecord& result) noexcept {
        if (bankData == nullptr || recordOffset > bankSize ||
            bankSize - recordOffset < 12)
            return false;
        MASSResourceSelectionRecord parsed{};
        if (recordOffset <= 0xffffu)
            parsed.xdataAddress = MASSXDataAddress::fromLinear(
                static_cast<std::uint16_t>(recordOffset));
        std::copy_n(bankData + recordOffset, parsed.bytes.size(), parsed.bytes.begin());
        result = parsed;
        return true;
    }
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
    // Parses a descriptor from the 64-KB memory window selected by the
    // Part's P3 mask (selection-record byte +9). The 512 descriptors in the
    // supplied IC51 image are all structurally valid in their pointer bank.
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
    MASSResourceCommandKind kind = MASSResourceCommandKind::conditionalRecord;
    std::uint8_t length = 0;
    // True when a group-0 record branches to 0x224C at byte +3. The
    // nested handler can still suppress the physical hardware-port write.
    bool branchesToExtendedCommand = false;
    std::array<std::uint8_t, 8> bytes{};
};

// Register-level input maps recovered directly from command handlers.
// These types deliberately retain 8051 register names: they do not assign
// unsupported musical meanings to individual bytes.
struct MASSPitchParameterRegisterFields {
    std::uint8_t opcode = 0; // F0
    std::uint8_t r2 = 0;     // stream byte +1
    std::uint8_t r3 = 0;     // stream byte +2
    std::uint8_t r4 = 0;     // stream byte +3
    std::uint8_t r7 = 0;     // stream byte +4
    std::uint8_t r6 = 0;     // stream byte +5
    std::uint8_t r5 = 0;     // stream byte +6
};

struct MASSMultiParameterRegisterFields {
    std::uint8_t opcode = 0; // R2
    std::uint8_t r3 = 0;     // stream byte +1 (zero selects skip/filler path)
    std::uint8_t r5 = 0;     // stream byte +2
    std::uint8_t r6 = 0;     // stream byte +3
    std::uint8_t f0 = 0;     // stream byte +4
    std::uint8_t r4 = 0;     // stream byte +5
    std::uint8_t r7 = 0;     // stream byte +6

    bool takesFillerPath() const noexcept { return r3 == 0; }
};

class MASSResourceCommandFieldDecoder {
public:
    static bool decodePitchParameter(const MASSResourceCommand& command,
                                     MASSPitchParameterRegisterFields& result) noexcept {
        if (command.kind != MASSResourceCommandKind::pitchParameter ||
            command.length != 7 || (command.opcode & 0x38) != 0x08)
            return false;
        result = {command.bytes[0], command.bytes[1], command.bytes[2],
                  command.bytes[3], command.bytes[4], command.bytes[5],
                  command.bytes[6]};
        return true;
    }

    static bool decodeMultiParameter(const MASSResourceCommand& command,
                                     MASSMultiParameterRegisterFields& result) noexcept {
        if ((command.kind != MASSResourceCommandKind::multiParameter &&
             command.kind != MASSResourceCommandKind::emptyMultiParameter) ||
            command.length != 7 || (command.opcode & 0x38) != 0x10)
            return false;
        result = {command.bytes[0], command.bytes[1], command.bytes[2],
                  command.bytes[3], command.bytes[4], command.bytes[5],
                  command.bytes[6]};
        return true;
    }
};

struct MASSHardwareCommandPacket {
    // 0x0DB7 writes R5, R2, R3, R4 to XDATA 0x8000..0x8003.
    std::array<std::uint8_t, 4> bytes{};
};

struct MASSResolvedMultiParameterRegisters {
    std::uint8_t ram2B = 0;
    std::uint8_t r2 = 0;
    std::uint8_t r3 = 0;
    std::uint8_t r4 = 0;
    std::uint8_t r5 = 0;
    std::uint8_t r6 = 0;
    std::uint8_t r7 = 0;
};

struct MASSHardwareCommandPacketSequence {
    std::array<MASSHardwareCommandPacket, 3> packets{};
    std::uint8_t count = 0;
};

class MASSResourceHardwareCommandBuilder {
public:
    // Terminal opcode group 0x20 / handler 0x2301. The first two payload
    // bytes are retained in the final slot, but the port packet takes its
    // middle bytes from direct RAM addresses 0x07 and 0x02 after helpers.
    static bool buildTerminalPacket(const MASSResourceCommand& command,
                                    std::uint8_t ram2B,
                                    std::uint8_t direct07,
                                    std::uint8_t direct02,
                                    MASSHardwareCommandPacket& result) noexcept {
        if (command.kind != MASSResourceCommandKind::threePayloadCommand ||
            command.length != 4 || (command.opcode & 0x38) != 0x20)
            return false;

        MASSHardwareCommandPacket packet{};
        packet.bytes = {
            static_cast<std::uint8_t>(ram2B | 0x0f),
            direct07,
            direct02,
            static_cast<std::uint8_t>(command.bytes[3] & 0x07)
        };
        result = packet;
        return true;
    }

    // Handler 0x1EDD / group 0x10 packet phase. Inputs are the post-transform
    // register values at the point the firmware reaches its external writes.
    // When emitBufferedUpdate is true, the first packet carries the three
    // bytes already staged in the current slot. The next two packets always
    // use [R5,R6,R7] and [R2,R3,R4], respectively.
    static bool buildMultiParameterPackets(
        const MASSResourceCommand& command,
        const MASSResolvedMultiParameterRegisters& registers,
        bool emitBufferedUpdate,
        const std::array<std::uint8_t, 3>& bufferedData,
        MASSHardwareCommandPacketSequence& result) noexcept {
        if (command.kind != MASSResourceCommandKind::multiParameter ||
            command.length != 7 || (command.opcode & 0x38) != 0x10 ||
            command.bytes[1] == 0)
            return false;

        MASSHardwareCommandPacketSequence sequence{};
        if (emitBufferedUpdate) {
            sequence.packets[sequence.count++].bytes = {
                static_cast<std::uint8_t>(registers.ram2B - 2),
                bufferedData[0], bufferedData[1], bufferedData[2]
            };
        }
        sequence.packets[sequence.count++].bytes = {
            registers.ram2B, registers.r5, registers.r6, registers.r7
        };
        sequence.packets[sequence.count++].bytes = {
            static_cast<std::uint8_t>(registers.ram2B - 1),
            registers.r2, registers.r3, registers.r4
        };
        result = sequence;
        return true;
    }

    // Handler 0x224C reached from group 0x00 at byte +3. The leading
    // three bytes belong to the outer conditional record; the nested handler
    // interprets bytes +3..+6 as opcode and three payload bytes.
    static bool buildConditionalExtendedPacket(
        const MASSResourceCommand& command,
        std::uint8_t ram2B,
        bool firmwareFlag08Set,
        MASSHardwareCommandPacket& result) noexcept {
        if (firmwareFlag08Set ||
            command.kind != MASSResourceCommandKind::conditionalRecord ||
            command.length != 7 || !command.branchesToExtendedCommand)
            return false;

        MASSHardwareCommandPacket packet{};
        packet.bytes = {
            ram2B,
            command.bytes[4],
            command.bytes[5],
            static_cast<std::uint8_t>(command.bytes[6] & 0x07)
        };
        result = packet;
        return true;
    }

    // Handler 0x224C / group 0x18. The flag-0x08 branch exits before the
    // record is read. On the normal branch the hardware packet receives the
    // command byte plus R2, R3 and the three-bit R4 payload.
    static bool buildFourByteParameterPacket(const MASSResourceCommand& command,
                                             std::uint8_t ram2B,
                                             bool firmwareFlag08Set,
                                             MASSHardwareCommandPacket& result) noexcept {
        if (firmwareFlag08Set ||
            command.kind != MASSResourceCommandKind::fourByteCommand ||
            command.length != 4 || (command.opcode & 0x38) != 0x18)
            return false;

        MASSHardwareCommandPacket packet{};
        packet.bytes = {
            ram2B,
            command.bytes[1],
            command.bytes[2],
            static_cast<std::uint8_t>(command.bytes[3] & 0x07)
        };
        result = packet;
        return true;
    }
};

struct MASSDecodedResourceStream {
    std::uint16_t streamPointer = 0;
    std::size_t endOffset = 0;
    std::size_t bytesConsumed = 0;
    std::uint8_t slotsConsumed = 0;
    bool terminated = false; // saw the explicit 0xFF stream terminator
    bool endedByControlCommand = false; // opcode group 0x20 returns via 0x19CD
    bool fillsRemainingSlots = false; // group 0x20 emits 0x38 filler slots when flag 0x0A is clear
    bool truncated = false;
    bool slotLimitReached = false;
    // Commands consumed differ from output slots written because terminal
    // group 0x20 can fill the remaining slots with 0x38 records.
    std::uint8_t outputSlotsWritten = 0;
    std::uint8_t fillerSlotsWritten = 0;
    std::vector<MASSResourceCommand> commands;
};

class MASSCommandStreamDecoder {
public:
    static constexpr std::size_t kMaximumSlots = 16;

    // Decodes the memory window selected by P3 flags loaded from byte +9 of
    // the 12-byte XDATA resource-selection record. The same P3 mask is used
    // for the IC51 pointer-table read; the caller supplies the resulting
    // memory window because the P2/page mapping is not yet reconstructed.
    // firmwareFlag0ASet changes the group-0 path: when clear, byte +3 bit 3
    // can branch to handler 0x224C. Its port write remains flag-dependent.
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
                        command.branchesToExtendedCommand = true;
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
            ++decoded.outputSlotsWritten;

            // Handler 0x2301 always leaves the interpreter via 0x19CD after
            // consuming this four-byte command. When flag 0x0A is clear it
            // also fills the remainder of the 16-slot output window with
            // 0x38 records before issuing the control packet.
            if (group == 0x20) {
                decoded.endedByControlCommand = true;
                // Handler 0x2301 fills all unoccupied 8-byte output slots
                // with 0x38 when flag 0x0A is clear. When it is set, firmware
                // jumps straight to the reserved final slot at offset 0xF8.
                decoded.fillsRemainingSlots = !firmwareFlag0ASet;
                if (decoded.fillsRemainingSlots) {
                    decoded.fillerSlotsWritten = static_cast<std::uint8_t>(
                        kMaximumSlots - decoded.outputSlotsWritten);
                    decoded.outputSlotsWritten =
                        static_cast<std::uint8_t>(kMaximumSlots);
                }
                break;
            }
        }

        decoded.endOffset = cursor;
        decoded.bytesConsumed = cursor - streamPointer;
        decoded.slotLimitReached =
            decoded.slotsConsumed == kMaximumSlots &&
            !decoded.terminated && !decoded.endedByControlCommand &&
            !decoded.truncated;
        result = std::move(decoded);
        return true;
    }
};

} // namespace technox
