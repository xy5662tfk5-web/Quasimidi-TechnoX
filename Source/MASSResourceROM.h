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
    bool readCodeByte(std::uint16_t address, std::uint8_t& value) const noexcept {
        if (image_ == nullptr || static_cast<std::size_t>(address) >= imageSize_)
            return false;
        value = image_[address];
        return true;
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

struct MASS1C61SecondaryTableAddress {
    std::uint16_t address = 0;
    std::uint8_t normalizedIndex = 0;
    std::uint16_t bankOffset = 0;
};

// Models the 0x1D2F..0x1D98 address selection before three MOVC reads.
// R3+RAM[0x2F] is wrapped into 0..0x7F by repeated +/-0x0C steps.
// R4.4 selects the secondary area by adding 0x180 to the base pointer.
class MASS1C61SecondaryTableAddressBuilder {
public:
    static MASS1C61SecondaryTableAddress resolve(std::uint16_t basePointer,
                                                  std::uint8_t r3,
                                                  std::uint8_t ram2F,
                                                  std::uint8_t r4) noexcept {
        auto index = static_cast<std::uint8_t>(r3 + ram2F);
        while ((index & 0x80u) != 0) {
            if ((index & 0x40u) != 0)
                index = static_cast<std::uint8_t>(index + 0x0cu);
            else
                index = static_cast<std::uint8_t>(index - 0x0cu);
        }
        const std::uint16_t bankOffset = (r4 & 0x10u) != 0 ? 0x0180u : 0u;
        return {
            static_cast<std::uint16_t>(basePointer + bankOffset + index),
            index,
            bankOffset
        };
    }
};

struct MASS1C61SecondaryTableFetchAddress {
    std::uint16_t address = 0;
    bool pageCarry = false;
};

// At 0x1D95 the original R7 is added to DPL; carry increments DPH before
// the first MOVC. This CY only affects the fetch address.
class MASS1C61SecondaryTableFetchAddressBuilder {
public:
    static MASS1C61SecondaryTableFetchAddress addOriginalR7(
        std::uint16_t baseAddress, std::uint8_t originalR7) noexcept {
        const auto lowSum = static_cast<unsigned>(baseAddress & 0xffu) + originalR7;
        const auto address = static_cast<std::uint16_t>(baseAddress + originalR7);
        return {address, lowSum > 0xffu};
    }
};

struct MASS1C61SecondaryTableBytes {
    std::array<std::uint8_t, 3> bytes{};
};

struct MASS1C61AccumulatorResult {
    std::uint8_t r2 = 0;
    std::uint8_t r3 = 0;
    std::uint8_t r4 = 0;
    bool zeroInputBypass = false;
    bool negativeInput = false;
};

// Instruction-level model of 0x1DC8..0x1E04. The input CY from ADD A,R7 at
// 0x1D95 only selects the MOVC page; RLC A consumes it, but that rotated A
// is overwritten before the second MUL. The saved CY used by ADDC is the
// old bit7 of the first MUL low byte, so no address carry enters this math.
class MASS1C61AccumulatorTransform {
public:
    static MASS1C61AccumulatorResult apply(
        std::uint8_t r2,
        std::uint8_t r3,
        std::uint8_t r4,
        std::uint8_t r5,
        std::uint8_t r6,
        std::uint8_t r7,
        std::uint8_t directRam05,
        std::uint8_t directRam06,
        std::uint8_t directRam07) noexcept {
        if (r2 == 0) {
            return {directRam05, directRam06, directRam07, true, false};
        }

        const bool negative = (r2 & 0x80u) != 0;
        const auto magnitude = negative
            ? static_cast<std::uint8_t>(0u - r2) : r2;

        const auto firstProduct = static_cast<std::uint16_t>(magnitude) * r3;
        const auto firstLow = static_cast<std::uint8_t>(firstProduct & 0xffu);
        const auto firstHigh = static_cast<std::uint8_t>(firstProduct >> 8);

        // RLC A consumes the prior address carry, but A is overwritten by
        // MOV A,R2 before the next MUL. Only the outgoing carry survives PSW.
        const auto carryFromRotate = (firstLow & 0x80u) != 0;

        const auto secondProduct = static_cast<std::uint16_t>(magnitude) * r4;
        const auto secondLow = static_cast<std::uint8_t>(secondProduct & 0xffu);
        const auto secondHigh = static_cast<std::uint8_t>(secondProduct >> 8);

        // ADDC A,R3 followed by XCH A,F0 and ADDC A,#0.
        const auto lowSum = static_cast<unsigned>(secondLow) +
                            static_cast<unsigned>(firstHigh) +
                            (carryFromRotate ? 1u : 0u);
        auto outR2 = static_cast<std::uint8_t>(lowSum & 0xffu);
        const bool carryToHigh = lowSum > 0xffu;

        const auto highSum = static_cast<unsigned>(secondHigh) +
                             (carryToHigh ? 1u : 0u);
        auto outR3 = static_cast<std::uint8_t>(highSum & 0xffu);
        std::uint8_t outR4 = 0;

        if (negative) {
            const auto negLow = static_cast<unsigned>(
                static_cast<std::uint8_t>(~outR2)) + 1u;
            outR2 = static_cast<std::uint8_t>(negLow & 0xffu);
            const bool negCarry = negLow > 0xffu;

            const auto negHigh = static_cast<unsigned>(
                static_cast<std::uint8_t>(~outR3)) + (negCarry ? 1u : 0u);
            outR3 = static_cast<std::uint8_t>(negHigh & 0xffu);
            const bool negCarryOut = negHigh > 0xffu;

            // Firmware only sets the 24-bit sign extension when the negated
            // 16-bit value did not wrap through zero.
            if (!negCarryOut)
                outR4 = 0xff;
        }

        const auto addLow = static_cast<unsigned>(outR2) + r5;
        outR2 = static_cast<std::uint8_t>(addLow & 0xffu);
        const bool c0 = addLow > 0xffu;

        const auto addMid = static_cast<unsigned>(outR3) + r6 + (c0 ? 1u : 0u);
        outR3 = static_cast<std::uint8_t>(addMid & 0xffu);
        const bool c1 = addMid > 0xffu;

        const auto addHigh = static_cast<unsigned>(outR4) + r7 + (c1 ? 1u : 0u);
        outR4 = static_cast<std::uint8_t>(addHigh & 0xffu);
        outR4 &= 0x07u;

        return {outR2, outR3, outR4, false, negative};
    }
};

struct MASS1C61PackedLookupFields {
    std::uint8_t r3 = 0;
    std::uint8_t r4 = 0;
    std::uint8_t r5 = 0;
    std::uint8_t r6 = 0;
    std::uint8_t r7 = 0;
};

// Reverses the SWAP/RL/ANL/ORL sequence at 0x1D9D..0x1DBD.
class MASS1C61PackedLookupDecoder {
public:
    static MASS1C61PackedLookupFields decode(
        const MASS1C61SecondaryTableBytes& table) noexcept {
        const auto packed0 = table.bytes[0];
        const auto packed1 = table.bytes[1];
        const auto packed2 = table.bytes[2];

        const auto a0 = rotateLeft(swapNibbles(packed0));
        const auto r3Low = static_cast<std::uint8_t>(a0 & 0x1fu);

        const auto a1 = rotateLeft(swapNibbles(packed1));
        const auto r3 = static_cast<std::uint8_t>((a1 & 0xe0u) | r3Low);
        auto r4 = static_cast<std::uint8_t>(a1 & 0x1fu);

        const auto r7 = packed2;
        const auto a2 = rotateLeft(swapNibbles(packed2));
        r4 = static_cast<std::uint8_t>(a2 | r4);

        return {r3, r4, packed0, packed1, r7};
    }

private:
    static constexpr std::uint8_t swapNibbles(std::uint8_t value) noexcept {
        return static_cast<std::uint8_t>((value << 4) | (value >> 4));
    }
    static constexpr std::uint8_t rotateLeft(std::uint8_t value) noexcept {
        return static_cast<std::uint8_t>((value << 1) | (value >> 7));
    }
};

class MASS1C61SecondaryTableReader {
public:
    // The firmware reads MOVC at DPTR, DPTR+0x80, then DPTR+0x100.
    static bool read(const MASSFirmwareROMView& rom,
                     std::uint16_t baseAddress,
                     MASS1C61SecondaryTableBytes& result) noexcept {
        MASS1C61SecondaryTableBytes decoded{};
        if (!rom.readCodeByte(baseAddress, decoded.bytes[0]) ||
            !rom.readCodeByte(static_cast<std::uint16_t>(baseAddress + 0x80u),
                              decoded.bytes[1]) ||
            !rom.readCodeByte(static_cast<std::uint16_t>(baseAddress + 0x100u),
                              decoded.bytes[2]))
            return false;
        result = decoded;
        return true;
    }

    static bool read(const MASSFirmwareROMView& rom,
                     const MASS1C61SecondaryTableAddress& address,
                     std::uint8_t originalR7,
                     MASS1C61SecondaryTableBytes& result,
                     MASS1C61SecondaryTableFetchAddress* resolved = nullptr) noexcept {
        const auto fetch = MASS1C61SecondaryTableFetchAddressBuilder::addOriginalR7(
            address.address, originalR7);
        if (resolved != nullptr)
            *resolved = fetch;
        return read(rom, fetch.address, result);
    }

    static bool read(const MASSFirmwareROMView& rom,
                     const MASS1C61SecondaryTableAddress& address,
                     MASS1C61SecondaryTableBytes& result) noexcept {
        return read(rom, address.address, result);
    }

    static bool read(const MASSFirmwareROMView& rom,
                     const MASS1C61SecondaryTableFetchAddress& address,
                     MASS1C61SecondaryTableBytes& result) noexcept {
        return read(rom, address.address, result);
    }
};

struct MASS1C61InterpolationLookupResult {
    bool lookupPerformed = false;
    std::uint8_t signedIndex = 0;
    std::uint8_t shiftCount = 0;
    std::uint8_t tableRemainder = 0;
    std::uint8_t coefficient = 0;
    bool negativeBranch = false;
};

// Reconstructs 0x1CC9..0x1CDE / 0x1D23..0x1D2A. The signed control byte
// comes from XDATA 0x2067; its bit7 selects the negative table. Negative
// values are two's-complement-negated before division by 12.
class MASS1C61InterpolationLookup {
public:
    static bool resolve(const MASSFirmwareROMView& rom,
                        std::uint8_t interpolationControl,
                        MASS1C61InterpolationLookupResult& result) noexcept {
        MASS1C61InterpolationLookupResult decoded{};
        if (interpolationControl == 0) {
            result = decoded; // firmware exits the interpolation path at 0x1CCD
            return true;
        }

        const bool negative = (interpolationControl & 0x80u) != 0;
        const auto index = negative
            ? static_cast<std::uint8_t>(0u - interpolationControl)
            : interpolationControl;
        const auto quotient = static_cast<std::uint8_t>(index / 12u);
        const auto remainder = static_cast<std::uint8_t>(index % 12u);
        const auto table = negative
            ? MASSFirmwareLookupTable::interpolationCurve11FC
            : MASSFirmwareLookupTable::interpolationCurve11F0;

        std::uint8_t coefficient = 0;
        if (!rom.read(table, remainder, coefficient))
            return false;

        decoded.lookupPerformed = true;
        decoded.signedIndex = index;
        decoded.shiftCount = quotient;
        decoded.tableRemainder = remainder;
        decoded.coefficient = coefficient;
        decoded.negativeBranch = negative;
        result = decoded;
        return true;
    }
};

struct MASS1C61Interpolation24Result {
    std::uint8_t r2 = 0;
    std::uint8_t r3 = 0;
    std::uint8_t r4 = 0;
    std::uint32_t value24 = 0;
};

// Executes the byte-level multiply and RLC/RRC loops after the 0x1C61
// interpolation table lookup. The 24-bit operand is R4:R3:R2, with R4
// already masked to three bits; the control byte from XDATA 0x2067 selects
// the coefficient and shift count independently.
class MASS1C61InterpolationArithmetic {
public:
    static MASS1C61Interpolation24Result apply(
        const MASS1C61InterpolationLookupResult& lookup,
        std::uint8_t r2,
        std::uint8_t r3,
        std::uint8_t r4) noexcept {
        const auto input = (static_cast<std::uint32_t>(r4 & 0x07u) << 16) |
                           (static_cast<std::uint32_t>(r3) << 8) | r2;
        if (!lookup.lookupPerformed) {
            return {r2, r3, static_cast<std::uint8_t>(r4 & 0x07u), input};
        }
        const auto product = static_cast<std::uint64_t>(input) * lookup.coefficient;
        auto value = static_cast<std::uint32_t>((product >> 8) & 0x00ffffffu);

        const auto shiftCount = static_cast<unsigned>(
            lookup.shiftCount + (lookup.negativeBranch ? 0u : 1u));
        unsigned carry = 0;

        for (unsigned i = 0; i < shiftCount; ++i) {
            auto low = static_cast<std::uint8_t>(value & 0xffu);
            auto middle = static_cast<std::uint8_t>((value >> 8) & 0xffu);
            auto high = static_cast<std::uint8_t>((value >> 16) & 0xffu);

            if (!lookup.negativeBranch) {
                auto next = static_cast<unsigned>(low >> 7);
                low = static_cast<std::uint8_t>((low << 1) | carry);
                carry = next;
                next = static_cast<unsigned>(middle >> 7);
                middle = static_cast<std::uint8_t>((middle << 1) | carry);
                carry = next;
                next = static_cast<unsigned>(high >> 7);
                high = static_cast<std::uint8_t>((high << 1) | carry);
                carry = next;
            } else {
                auto next = static_cast<unsigned>(high & 1u);
                high = static_cast<std::uint8_t>((high >> 1) | (carry << 7));
                carry = next;
                next = static_cast<unsigned>(middle & 1u);
                middle = static_cast<std::uint8_t>((middle >> 1) | (carry << 7));
                carry = next;
                next = static_cast<unsigned>(low & 1u);
                low = static_cast<std::uint8_t>((low >> 1) | (carry << 7));
                carry = next;
            }

            value = (static_cast<std::uint32_t>(high) << 16) |
                    (static_cast<std::uint32_t>(middle) << 8) | low;
        }

        return {
            static_cast<std::uint8_t>(value & 0xffu),
            static_cast<std::uint8_t>((value >> 8) & 0xffu),
            static_cast<std::uint8_t>((value >> 16) & 0xffu),
            value
        };
    }
};

struct MASS1EDDLookupResult {
    bool lookupPerformed = false;
    bool bSignBranch = false;
    std::uint8_t productHighByte = 0;
    std::uint8_t tableIndex = 0;
    std::uint8_t curveValue = 0;
};

// Reconstructs IC4 0x1EDD arithmetic from 0x1F4A through the first MOVC at
// 0x1F6C. This is the exact first-stage table index only; later 0x1308 gating
// also depends on RAM[0x3F], RAM[0x40], and opcode bits.
class MASS1EDDLookupIndexBuilder {
public:
    static bool build(const MASSFirmwareROMView& rom,
                      std::uint8_t opcode,
                      std::uint8_t ram30,
                      std::uint8_t r4,
                      MASS1EDDLookupResult& result) noexcept {
        MASS1EDDLookupResult decoded{};
        if (r4 == 0) {
            result = decoded; // firmware substitutes R4=0xFF and skips this lookup
            return true;
        }

        const auto b = rotateLeft(r4); // MOV F0, RL R4
        const bool bSignBranch = (b & 0x80u) != 0;

        // JB ACC.0 or JNB ACC.6: use RAM[0x30] directly except when
        // opcode bit0 is clear and opcode bit6 is set.
        const auto source = ((opcode & 0x01u) != 0 || (opcode & 0x40u) == 0)
            ? ram30 : static_cast<std::uint8_t>(~ram30);
        auto a = rotateLeft(source);

        // The ordinary B.7-clear path complements A before MUL AB.
        if (!bSignBranch)
            a = static_cast<std::uint8_t>(~a);

        const auto product = static_cast<std::uint16_t>(a) * b;
        const auto high = static_cast<std::uint8_t>(product >> 8);
        std::uint8_t index = 0;

        if (bSignBranch) {
            // MUL AB, JB B.7: high product sign bit saturates to A=FF;
            // otherwise RL B followed by CLR C/RRC returns the high byte.
            index = (high & 0x80u) != 0 ? 0x7fu : high;
        } else {
            // MOV A,B; RL A; CPL A; CLR C; RRC A == (~high) >> 1.
            index = static_cast<std::uint8_t>(
                static_cast<std::uint8_t>(~high) >> 1);
        }

        std::uint8_t curve = 0;
        if (!rom.read(MASSFirmwareLookupTable::nonlinearScale1288,
                      index, curve))
            return false;

        decoded.lookupPerformed = true;
        decoded.bSignBranch = bSignBranch;
        decoded.productHighByte = high;
        decoded.tableIndex = index;
        decoded.curveValue = curve;
        result = decoded;
        return true;
    }

private:
    static constexpr std::uint8_t rotateLeft(std::uint8_t value) noexcept {
        return static_cast<std::uint8_t>((value << 1) | (value >> 7));
    }
};

struct MASS1EDDPostLookupResult {
    std::uint8_t r4 = 0xff;
    std::uint8_t coefficientB = 0;
    bool quantizeLookupUsed = false;
    bool fallbackSignCorrection = false;
};

// Models IC4 0x1F6F..0x1F9F after the 0x1288 curve byte has been obtained.
// This is separate from the index builder because the quantize gate depends
// on RAM[0x3F], RAM[0x40] and opcode bits.
class MASS1EDDPostLookup {
public:
    static bool apply(const MASSFirmwareROMView& rom,
                      std::uint8_t opcode,
                      std::uint8_t ram3F,
                      std::uint8_t ram40,
                      std::uint8_t r3,
                      std::uint8_t curveValue,
                      MASS1EDDPostLookupResult& result) noexcept {
        MASS1EDDPostLookupResult decoded{};

        const bool quantizeEnabled =
            (ram3F & 0x80u) == 0 &&
            (ram40 & 0x10u) != 0 &&
            (opcode & 0x04u) != 0;

        if (quantizeEnabled) {
            std::uint8_t mapped = 0;
            if (!rom.read(MASSFirmwareLookupTable::quantizeMap1308,
                          curveValue, mapped))
                return false;
            decoded.r4 = mapped;
            decoded.coefficientB = r3;
            decoded.quantizeLookupUsed = true;
            result = decoded;
            return true;
        }

        decoded.r4 = 0xff;
        const bool signCorrected =
            (opcode & 0x01u) == 0 && (opcode & 0x40u) != 0;
        decoded.fallbackSignCorrection = signCorrected;

        const auto multiplicand = signCorrected
            ? static_cast<std::uint8_t>(~r3) : r3;
        const auto product = static_cast<std::uint16_t>(multiplicand) * curveValue;
        const auto high = static_cast<std::uint8_t>(product >> 8);

        decoded.coefficientB = signCorrected
            ? static_cast<std::uint8_t>(r3 + high)
            : high;

        // Common 0x1F9F guard: do not leave B zero when R3 was nonzero.
        if (decoded.coefficientB == 0 && r3 != 0)
            decoded.coefficientB = 1;

        result = decoded;
        return true;
    }
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
    std::uint8_t combinedCoefficient = 0; // Used by the 0x41 coefficient-table branch.
    bool transformApplied = false;
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
            b,
            true
        };
        return true;
    }
};

// Alternate 0x1890 entry used by 0x23C7 when (slotOpcode & 0x41)
// is 0x01 or 0x40. This is the exact entry at 0x1890, not the full 0x1848
// routine: it joins the latter's second multiply/accumulate half at 0x1865.
class MASS1890Arithmetic {
public:
    static std::uint8_t apply(std::uint8_t accumulator, std::uint8_t r2) noexcept {
        auto b = r2;
        if ((accumulator & 0x80u) == 0) {
            swap(accumulator, b);
            accumulator = static_cast<std::uint8_t>(
                static_cast<std::uint8_t>(0u - accumulator) + 0x7fu);
        }

        bool tempBit3 = false; // internal bit address 0x03
        if ((accumulator & 0x80u) != 0) {
            tempBit3 = true;
            accumulator = negate(accumulator);
        }

        swap(accumulator, b);
        if ((accumulator & 0x80u) != 0) {
            tempBit3 = !tempBit3;
            accumulator = negate(accumulator);
        }

        accumulator = static_cast<std::uint8_t>(rotateLeft(accumulator) | 0x01u);
        const auto product = static_cast<std::uint16_t>(accumulator) * b;
        accumulator = static_cast<std::uint8_t>(product >> 8); // MUL AB, then XCH A,B
        if (tempBit3)
            accumulator = negate(accumulator);

        const auto sum = static_cast<unsigned>(accumulator) + r2;
        const auto raw = static_cast<std::uint8_t>(sum & 0xffu);
        const bool carry = sum > 0xffu;
        const bool overflow =
            ((static_cast<std::uint8_t>(~(accumulator ^ r2)) &
              static_cast<std::uint8_t>(accumulator ^ raw) & 0x80u) != 0);
        if (overflow)
            return carry ? 0x81u : 0x7fu;
        return raw;
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
};

class MASSModulationScaleDispatcher {
public:
    static bool apply(const MASSFirmwareROMView& rom,
                      std::uint8_t slotOpcode,
                      std::uint8_t ram41,
                      std::uint8_t ram43,
                      std::uint8_t r2,
                      MASSModulationScaleResult& result) noexcept {
        switch (slotOpcode & 0x41u) {
            case 0x41u:
                return MASSModulationScaleMode41::apply(
                    rom, slotOpcode, ram41, ram43, r2, result);
            case 0x40u:
                result = {ram41 != 0 ? MASS1890Arithmetic::apply(ram41, r2) : r2,
                          0, ram41 != 0};
                return true;
            case 0x01u:
                result = {ram43 != 0 ? MASS1890Arithmetic::apply(ram43, r2) : r2,
                          0, ram43 != 0};
                return true;
            case 0x00u:
                result = {r2, 0, false};
                return true;
            default:
                return false;
        }
    }
};

struct MASS1EDDSlotCoefficientResult {
    std::uint8_t coefficientB = 0;
    bool slotOpcodeScaleApplied = false;
    bool modulationApplied = false;
};

// Models 0x1FCB..0x2005 after the core 0x1EDD transform. It applies the
// optional RAM[0x31]/RAM[0x32] two-MUL scale, then the complete 0x23C7
// dispatch (0x41 table scale, 0x40/0x01 alternate 0x1890 arithmetic).
class MASS1EDDSlotCoefficientStage {
public:
    static bool apply(const MASSFirmwareROMView& rom,
                      std::uint8_t slotOpcode,
                      std::uint8_t initialCoefficientB,
                      std::uint8_t ram31,
                      std::uint8_t ram32,
                      std::uint8_t ram41,
                      std::uint8_t ram43,
                      MASS1EDDSlotCoefficientResult& result) noexcept {
        auto coefficient = initialCoefficientB;
        bool scaled = false;

        if ((slotOpcode & 0x02u) != 0 && coefficient != 0) {
            scaled = true;
            const auto firstA = rotateLeft(ram31);
            const auto firstProduct = static_cast<std::uint16_t>(firstA) * coefficient;
            coefficient = static_cast<std::uint8_t>(firstProduct >> 8);

            if (coefficient != 0) {
                const auto secondA = rotateLeft(coefficient);
                const auto secondProduct = static_cast<std::uint16_t>(secondA) * ram32;
                coefficient = static_cast<std::uint8_t>(secondProduct >> 8);
                if (coefficient == 0 && ram32 != 0)
                    coefficient = 1;
            }
        }

        bool modulation = false;
        if (ram41 != 0 || ram43 != 0) {
            MASSModulationScaleResult mod{};
            if (!MASSModulationScaleDispatcher::apply(
                    rom, slotOpcode, ram41, ram43, coefficient, mod))
                return false;
            coefficient = mod.value;
            modulation = mod.transformApplied;
        }

        result = {coefficient, scaled, modulation};
        return true;
    }

private:
    static constexpr std::uint8_t rotateLeft(std::uint8_t value) noexcept {
        return static_cast<std::uint8_t>((value << 1) | (value >> 7));
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

struct MASS1EDDParameterPipelineResult {
    std::uint8_t r4 = 0xff;
    std::uint8_t coefficientB = 0;
    bool nonlinearLookupPerformed = false;
    bool quantizeLookupUsed = false;
    bool externalReadRequired = false;
    bool externalTransformApplied = false;
    std::uint16_t externalAddress = 0;
    MASS1848Result externalTransform{};
};

// Connects the recovered 0x1EDD stages through its optional 0x2381/0x1848
// tail. externalByte is the byte already read by the caller from the
// P3-selected bus at externalAddress; physical bus emulation is not implied.
class MASS1EDDParameterPipeline {
public:
    static bool process(const MASSFirmwareROMView& rom,
                        std::uint8_t opcode,
                        std::uint8_t ram30,
                        std::uint8_t r3,
                        std::uint8_t r4,
                        std::uint8_t ram3F,
                        std::uint8_t ram40,
                        std::uint16_t basePointer,
                        std::uint8_t selectorInputA,
                        std::uint8_t ram2F,
                        std::uint8_t r7,
                        std::uint8_t externalByte,
                        MASS1EDDParameterPipelineResult& result) noexcept {
        MASS1EDDParameterPipelineResult decoded{};

        MASS1EDDLookupResult first{};
        if (!MASS1EDDLookupIndexBuilder::build(
                rom, opcode, ram30, r4, first))
            return false;

        if (!first.lookupPerformed) {
            // 0x1F46 substitutes R4=0xFF and jumps to the R7 tail.
            decoded.r4 = 0xff;
            decoded.coefficientB = r3; // B was loaded from R3 at 0x1F41.
        } else {
            MASS1EDDPostLookupResult post{};
            if (!MASS1EDDPostLookup::apply(
                    rom, opcode, ram3F, ram40, r3, first.curveValue, post))
                return false;
            decoded.r4 = post.r4;
            decoded.coefficientB = post.coefficientB;
            decoded.nonlinearLookupPerformed = true;
            decoded.quantizeLookupUsed = post.quantizeLookupUsed;
        }

        if (r7 != 0) {
            const auto external = MASS2381AddressBuilder::resolve(
                basePointer, selectorInputA, ram2F);
            decoded.externalReadRequired = true;
            decoded.externalAddress = external.address;
            decoded.externalTransform = MASS1848Arithmetic::execute(
                externalByte, r7, decoded.coefficientB);
            decoded.coefficientB = decoded.externalTransform.value;
            decoded.externalTransformApplied = true;
        }

        result = decoded;
        return true;
    }
};

struct MASSResourceSelection {
    MASSResourceBank bank = MASSResourceBank::bank0;
    std::uint8_t pointerIndex = 0;
    std::uint8_t p3BankMask = 0;
    std::uint8_t normalizedPartParameter = 0;
};

// Four-byte bulk-transfer entry consumed by IC4 routine 0xAF1A.
// R7 is multiplied by two, then the firmware reads:
//   [source high, source low, count high, count low].
// The nested DJNZ loops treat a zero low byte as a full 256-byte page.
struct MASSIC51BulkBlockHeader {
    std::uint16_t sourceAddress = 0;
    std::uint8_t countHigh = 0;
    std::uint8_t countLow = 0;

    std::size_t firmwareByteCount() const noexcept {
        const auto lowCount = countLow == 0 ? 0x100u : static_cast<unsigned>(countLow);
        return static_cast<std::size_t>(countHigh) * 0x100u + lowCount;
    }

    static bool parse(const std::uint8_t* bankData,
                      std::size_t bankSize,
                      std::uint8_t r7,
                      MASSIC51BulkBlockHeader& result) noexcept {
        const auto offset = static_cast<std::size_t>(r7) * 2u;
        if (bankData == nullptr || bankSize < 0x10000u ||
            offset + 4u > bankSize)
            return false;
        MASSIC51BulkBlockHeader parsed{};
        parsed.sourceAddress = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bankData[offset]) << 8u) |
            bankData[offset + 1u]);
        parsed.countHigh = bankData[offset + 2u];
        parsed.countLow = bankData[offset + 3u];
        result = parsed;
        return true;
    }

    // MOVX @DPTR followed by INC DPTR wraps the 16-bit source address.
    static bool copyPayload(const std::uint8_t* bankData,
                            std::size_t bankSize,
                            const MASSIC51BulkBlockHeader& header,
                            std::uint8_t* destination,
                            std::size_t destinationSize) noexcept {
        const auto count = header.firmwareByteCount();
        if (bankData == nullptr || bankSize < 0x10000u ||
            destination == nullptr || destinationSize < count)
            return false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto address = static_cast<std::uint16_t>(
                static_cast<std::size_t>(header.sourceAddress) + i);
            destination[i] = bankData[address];
        }
        return true;
    }

    // IC4 0xAF05 maps each logical R0 byte through the firmware table at
    // 0xADDD, then writes it to the current 256-byte XDATA page. R0 wraps
    // naturally and increments P2, so each subsequent block uses the next
    // physical page. The caller supplies the exact 256-byte firmware map;
    // this avoids silently substituting a guessed permutation.
    static bool copyPayloadToPagedXdata(
        const std::uint8_t* bankData,
        std::size_t bankSize,
        const MASSIC51BulkBlockHeader& header,
        const std::uint8_t* logicalToPhysical,
        std::uint8_t* xdataWindow,
        std::size_t xdataWindowSize,
        std::uint16_t xdataBaseAddress = 0xc000u,
        std::uint16_t* lastPhysicalAddress = nullptr) noexcept {
        const auto count = header.firmwareByteCount();
        if (bankData == nullptr || bankSize < 0x10000u ||
            logicalToPhysical == nullptr || xdataWindow == nullptr ||
            xdataBaseAddress < 0xc000u || xdataBaseAddress > 0xffffu ||
            xdataWindowSize > 0x10000u - xdataBaseAddress)
            return false;
        const auto pageCount = (count + 0xffu) / 0x100u;
        if (pageCount * 0x100u > xdataWindowSize)
            return false;

        for (std::size_t i = 0; i < count; ++i) {
            const auto pageOffset = (i & ~std::size_t{0xff});
            const auto physicalOffset = pageOffset +
                logicalToPhysical[i & 0xffu];
            if (physicalOffset >= xdataWindowSize)
                return false;
            const auto sourceAddress = static_cast<std::uint16_t>(
                static_cast<std::size_t>(header.sourceAddress) + i);
            xdataWindow[physicalOffset] = bankData[sourceAddress];
            if (lastPhysicalAddress != nullptr && i + 1u == count)
                *lastPhysicalAddress = static_cast<std::uint16_t>(
                    xdataBaseAddress + physicalOffset);
        }
        return true;
    }
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

    const std::uint8_t* dataForBank(MASSResourceBank bank) const noexcept {
        if (!loaded_ || !validBank(bank)) return nullptr;
        return image_.data() + bankBase(bank);
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

// Resolves a Part bank/program pair through the firmware-derived IC51
// pointer table, descriptor parser and stream decoder. It recovers resource
// metadata and command streams only; it does not decode PCM or render audio.
struct MASSResolvedProgramResource {
    MASSResourceSelection selection{};
    std::uint8_t bankNumber = 0;
    std::uint8_t soundNumber = 0;
    std::uint16_t descriptorAddress = 0;
    MASSResourceDescriptor descriptor{};
    MASSDecodedResourceStream stream{};
};

class MASSResourceProgramResolver {
public:
    static bool resolve(const MASSResourceROM& rom,
                        int bankNumber,
                        int soundNumber,
                        MASSResolvedProgramResource& result,
                        bool firmwareFlag0ASet = false) {
        if (bankNumber < 0 || bankNumber > 3 || soundNumber < 0 || soundNumber > 127)
            return false;

        MASSResolvedProgramResource resolved{};
        resolved.bankNumber = static_cast<std::uint8_t>(bankNumber);
        resolved.soundNumber = static_cast<std::uint8_t>(soundNumber);
        if (!MASSResourceROM::resolvePartResource(
                bankNumber, resolved.soundNumber, resolved.selection))
            return false;
        if (!rom.readResourceDescriptorAddress(
                resolved.selection.bank, resolved.selection.pointerIndex,
                resolved.descriptorAddress))
            return false;

        const auto* bankData = rom.dataForBank(resolved.selection.bank);
        if (bankData == nullptr ||
            !MASSResourceDescriptorParser::parse(
                bankData, MASSResourceROM::kBankSize, resolved.descriptorAddress,
                resolved.descriptor))
            return false;
        if (!MASSCommandStreamDecoder::decode(
                bankData, MASSResourceROM::kBankSize,
                resolved.descriptor.commandStreamPointer, resolved.stream,
                firmwareFlag0ASet) || resolved.stream.truncated)
            return false;
        if (!resolved.stream.terminated && !resolved.stream.endedByControlCommand &&
            !resolved.stream.slotLimitReached)
            return false;

        result = std::move(resolved);
        return true;
    }
};


 // Startup-time decoded catalog. Audio-thread lookups are fixed-index and do
 // not parse descriptors or allocate vectors; each voice uses its captured
 // bank/program pair to select one immutable resolved stream.
class MASSResourceProgramCatalog {
public:
    static constexpr std::size_t kProgramCount = 4u * 128u;

    bool load(const MASSResourceROM& rom, bool firmwareFlag0ASet = false) {
        std::array<MASSResolvedProgramResource, kProgramCount> parsed{};
        for (int bank = 0; bank < 4; ++bank) {
            for (int sound = 0; sound < 128; ++sound) {
                auto& resource = parsed[static_cast<std::size_t>(bank * 128 + sound)];
                if (!MASSResourceProgramResolver::resolve(
                        rom, bank, sound, resource, firmwareFlag0ASet))
                    return false;
            }
        }
        resources_ = std::move(parsed);
        loaded_ = true;
        return true;
    }

    bool isLoaded() const noexcept { return loaded_; }

    const MASSResolvedProgramResource* find(int bank, int sound) const noexcept {
        if (!loaded_ || bank < 0 || bank >= 4 || sound < 0 || sound >= 128)
            return nullptr;
        return &resources_[static_cast<std::size_t>(bank * 128 + sound)];
    }

    void clear() noexcept {
        resources_ = {};
        loaded_ = false;
    }

private:
    std::array<MASSResolvedProgramResource, kProgramCount> resources_{};
    bool loaded_ = false;
};

} // namespace technox
