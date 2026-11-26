#pragma once

#include "MASSResourceROM.h"

#include <cstdint>

namespace technox {

// These values are snapshots from distinct IC4 instruction boundaries.
// Address-selection inputs are explicit because the current trace does not
// prove that they can always be reconstructed from the original group-08 bytes.
struct MASS1C61PitchExecutionSnapshot {
    std::uint8_t interpolationControl2067 = 0;
    bool hasCapturedInterpolationControl = false;

    // These values must be captured together at the 0x1D2F address boundary.
    std::uint16_t secondaryTableBasePointer = 0;
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
    std::uint8_t accumulatorInputR2 = 0;
    MASS1C61AccumulatorResult accumulator{};

    bool ready() const noexcept {
        return status == MASS1C61PitchExecutionStatus::ready;
    }
};

// Composes the already reconstructed IC4 group-08 stages:
//   1) command field extraction;
//   2) 0x2067 signed table lookup and 24-bit interpolation arithmetic;
//   3) 0x1D2F secondary table address selection and original-R7 fetch offset;
//   4) MOVC reads at DPTR, DPTR+0x80 and DPTR+0x100;
//   5) packed byte -> R3/R4/R5/R6/R7 decode;
//   6) 0x1DC8..0x1E04 24-bit accumulator transform.
//
// This is intentionally a snapshot-driven projection. The caller must supply
// the 0x1D2F address inputs from a captured handler boundary; the code does not
// guess their origin from the IC51 payload. The resulting accumulator is the
// pre-slot-correction value. The later 0x1E4B..0x1E66 guard/writeback remains a
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

        // At 0x1D45 the firmware overwrites the group-08 R7 byte with
        // the normalized (R3 + RAM[0x2F]) index. 0x1D95 adds that normalized
        // R7 to the loaded base pointer; commandFields.r7 is no longer live.
        if (!MASS1C61SecondaryTableReader::read(
                firmwareROM, result.secondaryAddress,
                result.secondaryTableBytes,
                &result.secondaryFetchAddress)) {
            result.status = MASS1C61PitchExecutionStatus::secondaryTableReadFailed;
            return result;
        }

        result.packedFields =
            MASS1C61PackedLookupDecoder::decode(result.secondaryTableBytes);

        if (!snapshot.hasCapturedAccumulatorInputR2) {
            result.status =
                MASS1C61PitchExecutionStatus::requiresAccumulatorInputR2Snapshot;
            return result;
        }
        result.accumulatorInputR2 = snapshot.accumulatorInputR2;

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
