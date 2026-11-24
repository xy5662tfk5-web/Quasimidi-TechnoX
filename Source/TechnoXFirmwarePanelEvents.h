#pragma once

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
// The low/high sample is preserved as an electrical state; it is not renamed
// clockwise/counter-clockwise until the full hardware phase sequence is proven.
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

} // namespace technox
