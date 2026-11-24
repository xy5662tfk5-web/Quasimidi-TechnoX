#pragma once

#include <array>
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


constexpr std::size_t kSequencerPartCount = 16;
using SequencerMidiChannelMap = std::array<int, kSequencerPartCount>;

constexpr SequencerMidiChannelMap defaultSequencerMidiChannelMap() noexcept
{
    SequencerMidiChannelMap result{};
    for (std::size_t part = 0; part < result.size(); ++part)
        result[part] = static_cast<int>(part + 1);
    return result;
}

constexpr bool isValidSequencerMidiChannelMap(
    const SequencerMidiChannelMap& channels) noexcept
{
    std::array<bool, 17> used{};
    for (const int channel : channels) {
        if (channel < 1 || channel > 16 || used[static_cast<std::size_t>(channel)])
            return false;
        used[static_cast<std::size_t>(channel)] = true;
    }
    return true;
}

constexpr int sequencerPartIndexForMidiChannel(
    const SequencerMidiChannelMap& channels, int midiChannel) noexcept
{
    if (midiChannel < 1 || midiChannel > 16)
        return -1;
    for (std::size_t part = 0; part < channels.size(); ++part)
        if (channels[part] == midiChannel)
            return static_cast<int>(part);
    return -1;
}

constexpr SequencerMidiChannelMap migrateLegacyPerformancePartChannels(
    const std::array<int, 4>& legacyChannels) noexcept
{
    auto result = defaultSequencerMidiChannelMap();
    std::array<bool, 17> used{};
    for (int channel = 1; channel <= 12; ++channel)
        used[static_cast<std::size_t>(channel)] = true;

    for (const int channel : legacyChannels) {
        if (channel < 13 || channel > 16 ||
            used[static_cast<std::size_t>(channel)])
            return defaultSequencerMidiChannelMap();
        used[static_cast<std::size_t>(channel)] = true;
    }

    for (std::size_t slot = 0; slot < legacyChannels.size(); ++slot)
        result[12 + slot] = legacyChannels[slot];
    return result;
}

} // namespace technox
