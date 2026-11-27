#pragma once

#include "MASSFirmwarePitchParameterExecution.h"

#include <cstdint>

namespace technox {

// Captured at the 0x1E4B guard/writeback boundary. These register values are
// kept independent from the earlier 1C61 accumulator output because the
// intervening correction path has not yet been completely proven.
struct MASSFirmwarePitchSlotCorrectionSnapshot {
    std::uint8_t xdata2018 = 0;
    std::uint8_t xdata2019 = 0;
    std::uint8_t incomingR4 = 0;
    std::uint8_t existingHigh = 0;
    std::uint8_t incomingR3 = 0;
    std::uint8_t incomingR2 = 0;
    bool hasCapturedGuardAndRegisters = false;
};

enum class MASSFirmwarePitchSlotCorrectionStatus : std::uint8_t {
    ready,
    upstreamPitchExecutionNotReady,
    requiresGuardAndRegisterSnapshot
};

struct MASSFirmwarePitchSlotCorrectionResult {
    MASSFirmwarePitchSlotCorrectionStatus status =
        MASSFirmwarePitchSlotCorrectionStatus::requiresGuardAndRegisterSnapshot;
    MASSPitchSlotGuardResult slot{};
    bool matchesPreCorrectionAccumulator = false;
    bool slotWriteSuppressedByZeroGuard = false;

    bool ready() const noexcept {
        return status == MASSFirmwarePitchSlotCorrectionStatus::ready;
    }
};

// Executes the separately reconstructed 0x1E4B..0x1E66 slot guard/writeback.
// It accepts a full register snapshot from that boundary instead of assuming
// that the 1C61 accumulator registers pass through unchanged. A comparison
// with the earlier accumulator is returned as diagnostic evidence only.
class MASSFirmwarePitchSlotCorrectionProjection {
public:
    static MASSFirmwarePitchSlotCorrectionResult execute(
        const MASS1C61PitchExecutionResult& pitchExecution,
        const MASSFirmwarePitchSlotCorrectionSnapshot& snapshot) noexcept {
        MASSFirmwarePitchSlotCorrectionResult result{};

        if (!pitchExecution.ready()) {
            result.status =
                MASSFirmwarePitchSlotCorrectionStatus::upstreamPitchExecutionNotReady;
            return result;
        }

        if (!snapshot.hasCapturedGuardAndRegisters) {
            result.status =
                MASSFirmwarePitchSlotCorrectionStatus::requiresGuardAndRegisterSnapshot;
            return result;
        }

        result.slot = MASSPitchSlotGuardProjection::apply(
            snapshot.xdata2018,
            snapshot.xdata2019,
            snapshot.incomingR4,
            snapshot.existingHigh,
            snapshot.incomingR3,
            snapshot.incomingR2);

        result.matchesPreCorrectionAccumulator =
            snapshot.incomingR2 == pitchExecution.accumulator.r2 &&
            snapshot.incomingR3 == pitchExecution.accumulator.r3 &&
            snapshot.incomingR4 == pitchExecution.accumulator.r4;
        result.slotWriteSuppressedByZeroGuard = !result.slot.writeSlot;
        result.status = MASSFirmwarePitchSlotCorrectionStatus::ready;
        return result;
    }
};

} // namespace technox
