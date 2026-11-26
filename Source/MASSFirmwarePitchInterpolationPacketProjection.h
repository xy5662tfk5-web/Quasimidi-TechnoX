#pragma once

#include "MASSResourceROM.h"

#include <cstdint>

namespace technox {

// Snapshot captured at the 0x09B9 caller boundary and the 0x0A4F writer
// boundary. The source bytes are the already-populated 24-bit slot; they are
// deliberately distinct from the earlier group-08/0x1C61 resource transform.
struct MASSFirmwarePitchInterpolationSnapshot {
    std::uint8_t slotLow = 0;
    std::uint8_t slotMiddle = 0;
    std::uint8_t slotHigh = 0;
    std::uint16_t stepMagnitude = 0;
    bool negativeStep = false;

    // R5 is copied by 0x0DB7 as packet byte zero. Keep it as a raw captured
    // firmware register and do not equate it with a DSP voice or sample ID.
    std::uint8_t packetR5 = 0;

    bool hasCapturedSlotTriplet = false;
    bool hasCapturedStepAndSign = false;
    bool hasCapturedPacketR5 = false;
};

enum class MASSFirmwarePitchInterpolationPacketStatus : std::uint8_t {
    ready,
    requiresSlotTriplet,
    requiresStepAndSign,
    requiresPacketR5
};

struct MASSFirmwarePitchInterpolationPacketResult {
    MASSFirmwarePitchInterpolationPacketStatus status =
        MASSFirmwarePitchInterpolationPacketStatus::requiresSlotTriplet;
    MASS09B9SlotInterpolationResult interpolation{};
    MASS0DB7PacketResult packet{};

    bool ready() const noexcept {
        return status == MASSFirmwarePitchInterpolationPacketStatus::ready;
    }
};

// End-to-end projection of the already traced:
//   slot triplet + signed 16-bit step
//       -> IC4 0x09B9/0x09DA arithmetic
//       -> 0x0A4F R2/R3/R4 writer inputs
//       -> 0x0DB7 four-byte packet and internal-bit-0x0C mirror.
//
// The 0x0A4F writer's R5 is supplied as an independent captured input. The
// group-08 / 0x1C61 accumulator and 0x1E4B slot correction remain upstream,
// separate stages: this helper does not pretend that one command record alone
// determines both the stored triplet and the later interpolation step.
class MASSFirmwarePitchInterpolationPacketProjection {
public:
    static MASSFirmwarePitchInterpolationPacketResult execute(
        const MASSFirmwarePitchInterpolationSnapshot& snapshot) noexcept {
        MASSFirmwarePitchInterpolationPacketResult result{};

        if (!snapshot.hasCapturedSlotTriplet) {
            result.status =
                MASSFirmwarePitchInterpolationPacketStatus::requiresSlotTriplet;
            return result;
        }
        if (!snapshot.hasCapturedStepAndSign) {
            result.status =
                MASSFirmwarePitchInterpolationPacketStatus::requiresStepAndSign;
            return result;
        }
        if (!snapshot.hasCapturedPacketR5) {
            result.status =
                MASSFirmwarePitchInterpolationPacketStatus::requiresPacketR5;
            return result;
        }

        result.interpolation = MASS09B9SlotInterpolationArithmetic::apply(
            snapshot.slotLow,
            snapshot.slotMiddle,
            snapshot.slotHigh,
            snapshot.stepMagnitude,
            snapshot.negativeStep);

        result.packet = MASS0DB7PacketBuilder::build(
            snapshot.packetR5,
            static_cast<std::uint8_t>(result.interpolation.outputValue24 & 0xffu),
            static_cast<std::uint8_t>(
                (result.interpolation.outputValue24 >> 8u) & 0xffu),
            static_cast<std::uint8_t>(
                (result.interpolation.outputValue24 >> 16u) & 0x07u));

        result.status = MASSFirmwarePitchInterpolationPacketStatus::ready;
        return result;
    }
};

} // namespace technox
