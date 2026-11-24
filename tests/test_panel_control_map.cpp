#include "../Source/TechnoXPanelControlMap.h"
#include <cassert>
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
    for (const mode : {PanelOperatingMode::performance,
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

    std::cout << "TechnoX manual-based panel control map tests passed\\n";
    return 0;
}
