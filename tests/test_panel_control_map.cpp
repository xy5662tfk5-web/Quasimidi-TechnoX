#include "../Source/TechnoXPanelControlMap.h"
#include <cassert>
#include <initializer_list>
#include <iostream>

int main()
{
    using namespace technox;

    // Performance mode main page: both Alpha dials select a Performance;
    // PART/BANK switches between ROM and RAM banks.
    assert(panelAction(PanelOperatingMode::performance, PanelEditPage::main,
                       PanelAlphaDial::parameterSoundGroup) ==
           PanelControlAction::selectPerformance);
    assert(panelAction(PanelOperatingMode::performance, PanelEditPage::main,
                       PanelAlphaDial::valueSound) ==
           PanelControlAction::selectPerformance);
    assert(panelAction(PanelOperatingMode::performance, PanelEditPage::main,
                       PanelKey::partBankNext) ==
           PanelControlAction::switchPerformanceBank);

    // Sequencer Multi main page: the first dial selects a sound group,
    // the second chooses a single sound, and PART/BANK selects a Part.
    assert(panelAction(PanelOperatingMode::sequencerMulti, PanelEditPage::main,
                       PanelAlphaDial::parameterSoundGroup) ==
           PanelControlAction::selectSoundGroup);
    assert(panelAction(PanelOperatingMode::sequencerMulti, PanelEditPage::main,
                       PanelAlphaDial::valueSound) ==
           PanelControlAction::selectSingleSound);
    assert(panelAction(PanelOperatingMode::sequencerMulti, PanelEditPage::main,
                       PanelKey::partBankPrevious) ==
           PanelControlAction::selectPart);

    // The Edit/Function menu is navigated with either Alpha dial.
    for (const auto mode : {PanelOperatingMode::performance,
                       PanelOperatingMode::sequencerMulti}) {
        assert(panelAction(mode, PanelEditPage::editFunctionMenu,
                           PanelAlphaDial::parameterSoundGroup) ==
               PanelControlAction::selectEditFunction);
        assert(panelAction(mode, PanelEditPage::editFunctionMenu,
                           PanelAlphaDial::valueSound) ==
               PanelControlAction::selectEditFunction);
        assert(panelAction(mode, PanelEditPage::editFunctionMenu,
                           PanelKey::partBankNext) == PanelControlAction::none);
    }

    // Edit System uses PARAMETER for a system item and VALUE for its value.
    assert(panelAction(PanelOperatingMode::performance, PanelEditPage::editSystem,
                       PanelAlphaDial::parameterSoundGroup) ==
           PanelControlAction::selectSystemParameter);
    assert(panelAction(PanelOperatingMode::performance, PanelEditPage::editSystem,
                       PanelAlphaDial::valueSound) ==
           PanelControlAction::editSystemParameter);

    // In Part Edit, the two Alpha dials have different edit roles in either
    // operating mode: select a parameter, then change its value.
    for (const auto mode : {PanelOperatingMode::performance,
                            PanelOperatingMode::sequencerMulti}) {
        assert(panelAction(mode, PanelEditPage::editPart,
                           PanelAlphaDial::parameterSoundGroup) ==
               PanelControlAction::selectEditParameter);
        assert(panelAction(mode, PanelEditPage::editPart,
                           PanelAlphaDial::valueSound) ==
               PanelControlAction::editSelectedParameter);
        assert(panelAction(mode, PanelEditPage::editPart,
                           PanelKey::partBankNext) ==
               PanelControlAction::selectPart);
    }

    // Sequencer Multi Part 1..16 defaults to MIDI channels 1..16.
    const auto defaultChannels = defaultSequencerMidiChannelMap();
    assert(isValidSequencerMidiChannelMap(defaultChannels));
    for (int channel = 1; channel <= 16; ++channel)
        assert(sequencerPartIndexForMidiChannel(defaultChannels, channel) == channel - 1);
    assert(sequencerPartIndexForMidiChannel(defaultChannels, 0) == -1);
    assert(sequencerPartIndexForMidiChannel(defaultChannels, 17) == -1);

    // A valid remap is independent of display order; invalid duplicate maps
    // must be rejected before they are restored from a plugin state chunk.
    auto remappedChannels = defaultChannels;
    remappedChannels[0] = 16;
    remappedChannels[15] = 1;
    assert(isValidSequencerMidiChannelMap(remappedChannels));
    assert(sequencerPartIndexForMidiChannel(remappedChannels, 16) == 0);
    assert(sequencerPartIndexForMidiChannel(remappedChannels, 1) == 15);
    remappedChannels[7] = 16;
    assert(!isValidSequencerMidiChannelMap(remappedChannels));

    // V1–V4 plugin states stored only the four legacy Performance Part
    // channels. Valid 13..16 assignments migrate to the Sequencer Part
    // slots 13..16 while Parts 1..12 keep their standard channels.
    const std::array<int, 4> legacyDefault{{13, 14, 15, 16}};
    const auto migratedDefault = migrateLegacyPerformancePartChannels(legacyDefault);
    assert(isValidSequencerMidiChannelMap(migratedDefault));
    for (int channel = 1; channel <= 16; ++channel)
        assert(sequencerPartIndexForMidiChannel(migratedDefault, channel) == channel - 1);

    const std::array<int, 4> legacyRemapped{{16, 13, 15, 14}};
    const auto migratedRemapped = migrateLegacyPerformancePartChannels(legacyRemapped);
    assert(isValidSequencerMidiChannelMap(migratedRemapped));
    assert(migratedRemapped[12] == 16);
    assert(migratedRemapped[13] == 13);
    assert(migratedRemapped[14] == 15);
    assert(migratedRemapped[15] == 14);

    const std::array<int, 4> legacyDuplicate{{13, 13, 15, 16}};
    assert(migrateLegacyPerformancePartChannels(legacyDuplicate) == defaultChannels);
    const std::array<int, 4> legacyCollision{{12, 13, 14, 16}};
    assert(migrateLegacyPerformancePartChannels(legacyCollision) == defaultChannels);
    const std::array<int, 4> legacyInvalid{{0, 13, 14, 16}};
    assert(migrateLegacyPerformancePartChannels(legacyInvalid) == defaultChannels);

    std::cout << "TechnoX manual-based panel control map tests passed\\n";
    return 0;
}
