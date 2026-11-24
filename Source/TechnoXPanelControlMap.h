#pragma once

#include <cstdint>

namespace technox {

enum class PanelOperatingMode : std::uint8_t {
    performance,
    sequencerMulti
};

enum class PanelEditPage : std::uint8_t {
    main,
    editFunctionMenu,
    editPart,
    editSystem
};

enum class PanelAlphaDial : std::uint8_t {
    parameterSoundGroup,
    valueSound
};

enum class PanelKey : std::uint8_t {
    partBankPrevious,
    partBankNext
};

enum class PanelControlAction : std::uint8_t {
    none,
    selectPerformance,
    switchPerformanceBank,
    selectSoundGroup,
    selectSingleSound,
    selectPart,
    selectEditFunction,
    selectEditParameter,
    selectSystemParameter,
    editSystemParameter,
    editSelectedParameter
};

// This table follows the original TechnoX Owner's Manual, pages 6–9.
// The encoder actions are deliberately independent from firmware GPIO channel
// numbers: IC4 exposes two scan channels, but the physical PARAMETER/VALUE
// label-to-channel wiring is not yet proven.
constexpr PanelControlAction panelAction(PanelOperatingMode mode,
                                         PanelEditPage page,
                                         PanelAlphaDial dial) noexcept
{
    if (page == PanelEditPage::editFunctionMenu)
        return PanelControlAction::selectEditFunction;

    if (page == PanelEditPage::editPart)
        return dial == PanelAlphaDial::parameterSoundGroup
            ? PanelControlAction::selectEditParameter
            : PanelControlAction::editSelectedParameter;

    if (page == PanelEditPage::editSystem)
        return dial == PanelAlphaDial::parameterSoundGroup
            ? PanelControlAction::selectSystemParameter
            : PanelControlAction::editSystemParameter;

    if (mode == PanelOperatingMode::performance)
        return PanelControlAction::selectPerformance;

    return dial == PanelAlphaDial::parameterSoundGroup
        ? PanelControlAction::selectSoundGroup
        : PanelControlAction::selectSingleSound;
}

constexpr PanelControlAction panelAction(PanelOperatingMode mode,
                                         PanelEditPage page,
                                         PanelKey) noexcept
{
    if (page == PanelEditPage::editPart)
        return PanelControlAction::selectPart;

    if (page != PanelEditPage::main)
        return PanelControlAction::none;

    return mode == PanelOperatingMode::performance
        ? PanelControlAction::switchPerformanceBank
        : PanelControlAction::selectPart;
}

} // namespace technox
