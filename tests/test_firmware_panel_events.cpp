#include "../Source/TechnoXFirmwarePanelEvents.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

int main()
{
    using namespace technox;
    struct Expected {
        std::uint8_t code;
        FirmwarePanelEncoderRole role;
        FirmwareEncoderDirectionSample sample;
    };
    constexpr std::array<Expected, 4> expected{{
        {0xbe, FirmwarePanelEncoderRole::valueSound,
              FirmwareEncoderDirectionSample::p1_6Low},
        {0x7e, FirmwarePanelEncoderRole::valueSound,
              FirmwareEncoderDirectionSample::p1_6High},
        {0xbd, FirmwarePanelEncoderRole::parameterSoundGroup,
              FirmwareEncoderDirectionSample::p1_6Low},
        {0x7d, FirmwarePanelEncoderRole::parameterSoundGroup,
              FirmwareEncoderDirectionSample::p1_6High}
    }};

    for (const auto& item : expected) {
        FirmwarePanelEncoderEvent event{};
        assert(decodeFirmwarePanelEncoderEvent(item.code, event));
        assert(event.originalCode == item.code);
        assert(event.role == item.role);
        assert(event.directionSample == item.sample);
    }

    for (const auto unknown : {0x00, 0xf0, 0xf1, 0x7c, 0xff}) {
        FirmwarePanelEncoderEvent event{};
        assert(!decodeFirmwarePanelEncoderEvent(
            static_cast<std::uint8_t>(unknown), event));
    }

    std::cout << "TechnoX IC4 firmware panel event decoder tests passed\\n";
    return 0;
}
