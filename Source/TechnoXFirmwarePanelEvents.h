#pragma once

#include "TechnoXPanelControlMap.h"
#include <cstdint>

namespace technox {

enum class FirmwarePanelEncoderRole : std::uint8_t {
    parameterSoundGroup,
    valueSound
};

enum class FirmwareEncoderDirectionSample : std::uint8_t {
    p1_6Low,
    p1_6High
};

struct FirmwarePanelEncoderEvent {
    FirmwarePanelEncoderRole role = FirmwarePanelEncoderRole::parameterSoundGroup;
    FirmwareEncoderDirectionSample directionSample = FirmwareEncoderDirectionSample::p1_6Low;
    std::uint8_t originalCode = 0;
};

// Decode the four event bytes emitted by IC4 v1.05 routine 0xA762.
// P1.6 is retained as an electrical sample, not guessed as clockwise or
// counter-clockwise: the complete physical phase/board sequence is unverified.
constexpr bool decodeFirmwarePanelEncoderEvent(
    std::uint8_t code, FirmwarePanelEncoderEvent& result) noexcept
{
    switch (code) {
        case 0xBE:
            result = {FirmwarePanelEncoderRole::valueSound,
                      FirmwareEncoderDirectionSample::p1_6Low, code};
            return true;
        case 0x7E:
            result = {FirmwarePanelEncoderRole::valueSound,
                      FirmwareEncoderDirectionSample::p1_6High, code};
            return true;
        case 0xBD:
            result = {FirmwarePanelEncoderRole::parameterSoundGroup,
                      FirmwareEncoderDirectionSample::p1_6Low, code};
            return true;
        case 0x7D:
            result = {FirmwarePanelEncoderRole::parameterSoundGroup,
                      FirmwareEncoderDirectionSample::p1_6High, code};
            return true;
        default:
            return false;
    }
}

constexpr PanelControlAction panelActionForFirmwareEvent(
    PanelOperatingMode mode, PanelEditPage page,
    const FirmwarePanelEncoderEvent& event) noexcept
{
    const auto dial = event.role == FirmwarePanelEncoderRole::parameterSoundGroup
        ? PanelAlphaDial::parameterSoundGroup : PanelAlphaDial::valueSound;
    return panelAction(mode, page, dial);
}

} // namespace technox
