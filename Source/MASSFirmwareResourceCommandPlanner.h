#pragma once

#include "MASSResourceROM.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace technox {

// This planner is deliberately downstream of firmware interpretation. Its
// inputs are register/flag snapshots captured at exact IC4 handler boundaries;
// it does not turn IC51 bytes directly into guessed SAM9407 register writes.
enum class MASSFirmwarePacketPlanStatus : std::uint8_t {
    ready,
    fillerSlotOnly,
    streamAdvanceOnly,
    extendedBranchSuppressed,
    requiresPitchHandlerState,
    requiresConditionalHandlerState,
    invalidCommand
};

struct MASSFirmwareCommandBoundaryState {
    // RAM[0x2B] carries the firmware's current parameter/slot selector here.
    // Its physical DSP meaning is intentionally not assigned.
    std::uint8_t ram2B = 0;

    // Firmware flag bits as observed at the command-handler boundary.
    bool flag08 = false;
    bool flag0A = false;
    bool flag0C = false;

    // State after the handler's transformations, at 0x20CC..0x210B packet
    // construction. These are IC4 register values, not inferred DSP fields.
    std::array<std::uint8_t, 3> recordTail{};
    std::uint8_t r2 = 0;
    std::uint8_t r3 = 0;
    std::uint8_t r4 = 0;
    std::uint8_t r5 = 0;
    std::uint8_t r6 = 0;
    std::uint8_t r7 = 0;

    // Values read from the direct-RAM operands used by handler 0x2301.
    std::uint8_t direct02 = 0;
    std::uint8_t direct07 = 0;
};

struct MASSFirmwarePacketPlan {
    MASSFirmwarePacketPlanStatus status = MASSFirmwarePacketPlanStatus::invalidCommand;
    std::array<MASSHardwareCommandPacket, 3> packets{};
    std::uint8_t packetCount = 0;

    bool ready() const noexcept {
        return status == MASSFirmwarePacketPlanStatus::ready;
    }
};

// Connects decoded IC51 stream command kinds to the packet builders whose
// byte ordering and masks have already been recovered from IC4. This is a
// packetization layer only: pitch records still require their full 1C61/
// pitch-handler snapshot, and no output packet is assigned SAM9407 semantics.
class MASSFirmwareResourceCommandPlanner {
public:
    static MASSFirmwarePacketPlan plan(
        const MASSResourceCommand& command,
        const MASSFirmwareCommandBoundaryState& state) noexcept {
        MASSFirmwarePacketPlan result{};

        switch (command.kind) {
            case MASSResourceCommandKind::conditionalRecord: {
                if (!command.branchesToExtendedCommand) {
                    if (command.length != 4u || (command.opcode & 0x38u) != 0x00u)
                        return result;
                    result.status =
                        MASSFirmwarePacketPlanStatus::requiresConditionalHandlerState;
                    return result;
                }
                if (state.flag08) {
                    result.status =
                        MASSFirmwarePacketPlanStatus::extendedBranchSuppressed;
                    return result;
                }
                MASSHardwareCommandPacket packet{};
                if (!MASSResourceHardwareCommandBuilder::buildConditionalExtendedPacket(
                        command, state.ram2B, false, packet))
                    return result;
                result.packets[0] = packet;
                result.packetCount = 1;
                result.status = MASSFirmwarePacketPlanStatus::ready;
                return result;
            }

            case MASSResourceCommandKind::pitchParameter: {
                MASSPitchParameterRegisterFields decoded{};
                if (!MASSResourceCommandFieldDecoder::decodePitchParameter(
                        command, decoded))
                    return result;
                // Group 0x08 enters the pitch/interpolation path. Its required
                // pointer, table, slot and bus-derived input state is not
                // represented by this packet-boundary snapshot.
                result.status = MASSFirmwarePacketPlanStatus::requiresPitchHandlerState;
                return result;
            }

            case MASSResourceCommandKind::multiParameter: {
                MASSMultiParameterRegisterFields decoded{};
                if (!MASSResourceCommandFieldDecoder::decodeMultiParameter(
                        command, decoded))
                    return result;
                if (decoded.takesFillerPath()) {
                    result.status = MASSFirmwarePacketPlanStatus::fillerSlotOnly;
                    return result;
                }
                const auto sequence = MASS1EDDRecordPacketSequenceBuilder::build(
                    state.flag0A, state.ram2B, state.recordTail, state.flag0C,
                    state.r5, state.r6, state.r7,
                    state.r2, state.r3, state.r4);
                if (sequence.count == 0u ||
                    sequence.count > result.packets.size())
                    return result;
                for (std::size_t i = 0; i < sequence.count; ++i)
                    result.packets[i].bytes = sequence.packets[i].bytes;
                result.packetCount = static_cast<std::uint8_t>(sequence.count);
                result.status = MASSFirmwarePacketPlanStatus::ready;
                return result;
            }

            case MASSResourceCommandKind::emptyMultiParameter: {
                MASSMultiParameterRegisterFields decoded{};
                if (!MASSResourceCommandFieldDecoder::decodeMultiParameter(
                        command, decoded) || !decoded.takesFillerPath())
                    return result;
                result.status = MASSFirmwarePacketPlanStatus::fillerSlotOnly;
                return result;
            }

            case MASSResourceCommandKind::fourByteCommand: {
                MASSHardwareCommandPacket packet{};
                if (state.flag08) {
                    result.status = MASSFirmwarePacketPlanStatus::extendedBranchSuppressed;
                    return result;
                }
                if (!MASSResourceHardwareCommandBuilder::buildFourByteParameterPacket(
                        command, state.ram2B, false, packet))
                    return result;
                result.packets[0] = packet;
                result.packetCount = 1;
                result.status = MASSFirmwarePacketPlanStatus::ready;
                return result;
            }

            case MASSResourceCommandKind::threePayloadCommand: {
                MASSHardwareCommandPacket packet{};
                if (!MASSResourceHardwareCommandBuilder::buildTerminalPacket(
                        command, state.ram2B, state.direct07, state.direct02, packet))
                    return result;
                result.packets[0] = packet;
                result.packetCount = 1;
                result.status = MASSFirmwarePacketPlanStatus::ready;
                return result;
            }

            case MASSResourceCommandKind::marker:
                if (command.length != 1u || (command.opcode & 0x38u) != 0x38u)
                    return result;
                result.status = MASSFirmwarePacketPlanStatus::fillerSlotOnly;
                return result;

            case MASSResourceCommandKind::singleByteAdvance:
                if (command.length != 1u ||
                    ((command.opcode & 0x38u) != 0x28u &&
                     (command.opcode & 0x38u) != 0x30u))
                    return result;
                result.status = MASSFirmwarePacketPlanStatus::streamAdvanceOnly;
                return result;
        }

        return result;
    }
};

} // namespace technox
