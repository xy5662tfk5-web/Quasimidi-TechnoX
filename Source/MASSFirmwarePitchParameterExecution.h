#pragma once

#include "MASSResourceROM.h"

#include <cstdint>

namespace technox {

// These values are snapshots from distinct IC4 instruction boundaries.
// The live Note-72 trace demonstrates that several values are rewritten
// between the resource record and the 1DC8 accumulator boundary.
struct MASS1C61PitchExecutionSnapshot {
    std::uint8_t interpolationControl2067 = 0;
    bool hasCapturedInterpolationControl = false;

    // Base DPTR loaded from IRAM[0x9E:0x9F] at 0x1D7C..0x1D81,
    // captured before the normalized R7 add at 0x1D95.
    std::uint16_t secondaryTableBasePointer = 0;
    // Inputs to the earlier 0x1D2F..0x1D45 normalization and bank selection.
    std::uint8_t secondaryTableIndexR3 = 0;
    std::uint8_t secondaryTableBankSelectR4 = 0;
    std::uint8_t ram2F = 0;
    bool hasCapturedSecondaryAddressInputs = false;

    // Direct RAM inputs are consulted only by the R2==0 accumulator bypass.
    std::uint8_t directRam05 = 0;
    std::uint8_t directRam06 = 0;
    std::uint8_t directRam07 = 0;
    bool hasCapturedDirectRamAccumulatorInputs = false;

    // R2 is further transformed by the live 0x1D51..0x1D78 route before the
    // accumulator at 0x1DC8. It is not always identical to the group-08 byte
    // or the 0x2067 interpolation projection (notably Note 72: 0x20 -> 0x80).
    // Optional live producer input for 0x1D51..0x1D78. When captured, the
    // projection derives both accumulator R2 and any OV-dependent R7 correction.
    std::uint8_t directRam34 = 0;
    bool hasCapturedAccumulatorInputSource = false;

    // Fallback for captured instruction-boundary R2 when RAM[0x34]/the
    // preceding 0x1D51 branch inputs are not yet available.
    std::uint8_t accumulatorInputR2 = 0;
    bool hasCapturedAccumulatorInputR2 = false;
};

enum class MASS1C61PitchExecutionStatus : std::uint8_t {
    ready,
    commandStreamExhausted,
    invalidCommand,
    firmwareROMUnavailable,
    requiresInterpolationControlSnapshot,
    interpolationLookupFailed,
    requiresSecondaryAddressSnapshot,
    secondaryTableReadFailed,
    requiresAccumulatorInputR2Snapshot,
    requiresDirectRamBypassSnapshot
};

struct MASS1C61PitchExecutionResult {
    MASS1C61PitchExecutionStatus status =
        MASS1C61PitchExecutionStatus::invalidCommand;
    MASSPitchParameterRegisterFields commandFields{};
    MASS1C61InterpolationLookupResult interpolationLookup{};
    MASS1C61Interpolation24Result interpolatedInput{};
    MASS1C61SecondaryTableAddress secondaryAddress{};
    MASS1C61SecondaryTableFetchAddress secondaryFetchAddress{};
    MASS1C61SecondaryTableBytes secondaryTableBytes{};
    MASS1C61PackedLookupFields packedFields{};
    MASS1C61AccumulatorInputRouteResult accumulatorInputRoute{};
    std::uint8_t accumulatorInputR2 = 0;
    MASS1C61AccumulatorResult accumulator{};

    bool ready() const noexcept {
        return status == MASS1C61PitchExecutionStatus::ready;
    }
};

// Composes the already reconstructed IC4 group-08 stages:
//   1) command field extraction;
//   2) 0x2067 signed table lookup and 24-bit interpolation projection;
//   3) 0x1D2F..0x1D45 normalized-index and 0x1D7C..0x1D84 base/bank selection;
//   4) add normalized live R7 at 0x1D95, then MOVC at DPTR/+0x80/+0x100;
//   5) packed byte -> R3/R4/R5/R6/R7 decode;
//   6) captured live R2 plus decoded fields -> 0x1DC8..0x1E04 accumulator.
//
// This is intentionally a snapshot-driven projection. The caller must supply
// the base pointer, address-selection inputs, and actual R2 at the 0x1DC8
// boundary; these are not interchangeable with the raw IC51 command bytes.
// The resulting accumulator is the pre-slot-correction value. The later 0x1E4B..0x1E66 guard/writeback remains a
// separate operation with its own XDATA snapshot.
class MASS1C61PitchParameterExecutionProjection {
public:
    static MASS1C61PitchExecutionResult execute(
        const MASSFirmwareROMView& firmwareROM,
        const MASSResourceCommand& command,
        const MASS1C61PitchExecutionSnapshot& snapshot) noexcept {
        MASS1C61PitchExecutionResult result{};

        if (!MASSResourceCommandFieldDecoder::decodePitchParameter(
                command, result.commandFields))
            return result;

        if (!firmwareROM.isAttached()) {
            result.status = MASS1C61PitchExecutionStatus::firmwareROMUnavailable;
            return result;
        }

        if (!snapshot.hasCapturedInterpolationControl) {
            result.status =
                MASS1C61PitchExecutionStatus::requiresInterpolationControlSnapshot;
            return result;
        }

        if (!MASS1C61InterpolationLookup::resolve(
                firmwareROM, snapshot.interpolationControl2067,
                result.interpolationLookup)) {
            result.status = MASS1C61PitchExecutionStatus::interpolationLookupFailed;
            return result;
        }

        result.interpolatedInput = MASS1C61InterpolationArithmetic::apply(
            result.interpolationLookup,
            result.commandFields.r2,
            result.commandFields.r3,
            result.commandFields.r4);

        if (!snapshot.hasCapturedSecondaryAddressInputs) {
            result.status =
                MASS1C61PitchExecutionStatus::requiresSecondaryAddressSnapshot;
            return result;
        }

        result.secondaryAddress =
            MASS1C61SecondaryTableAddressBuilder::resolve(
                snapshot.secondaryTableBasePointer,
                snapshot.secondaryTableIndexR3,
                snapshot.ram2F,
                snapshot.secondaryTableBankSelectR4);

        if (snapshot.hasCapturedAccumulatorInputSource) {
            result.accumulatorInputRoute =
                MASS1C61AccumulatorInputRouteProjection::apply(
                    result.commandFields.r2,
                    result.commandFields.r4,
                    snapshot.directRam34,
                    result.secondaryAddress.normalizedIndex);
            result.accumulatorInputR2 =
                result.accumulatorInputRoute.accumulatorR2;
            result.secondaryAddress.normalizedIndex =
                result.accumulatorInputRoute.normalizedR7;
        } else if (snapshot.hasCapturedAccumulatorInputR2) {
            result.accumulatorInputR2 = snapshot.accumulatorInputR2;
        } else {
            result.status =
                MASS1C61PitchExecutionStatus::requiresAccumulatorInputR2Snapshot;
            return result;
        }

        // At 0x1D45 the group-08 R7 byte is replaced by normalized R7.
        // The 0x1D51..0x1D78 route may make a further OV-dependent correction.
        // 0x1D95 adds the resulting live R7 exactly once to the loaded base DPTR.
        if (!MASS1C61SecondaryTableReader::read(
                firmwareROM, result.secondaryAddress,
                result.secondaryTableBytes,
                &result.secondaryFetchAddress)) {
            result.status = MASS1C61PitchExecutionStatus::secondaryTableReadFailed;
            return result;
        }

        result.packedFields =
            MASS1C61PackedLookupDecoder::decode(result.secondaryTableBytes);

        if (result.accumulatorInputR2 == 0u &&
            !snapshot.hasCapturedDirectRamAccumulatorInputs) {
            result.status =
                MASS1C61PitchExecutionStatus::requiresDirectRamBypassSnapshot;
            return result;
        }

        result.accumulator = MASS1C61AccumulatorTransform::apply(
            result.accumulatorInputR2,
            result.packedFields.r3,
            result.packedFields.r4,
            result.packedFields.r5,
            result.packedFields.r6,
            result.packedFields.r7,
            snapshot.directRam05,
            snapshot.directRam06,
            snapshot.directRam07);

        result.status = MASS1C61PitchExecutionStatus::ready;
        return result;
    }
};

} // namespace technox
