#include "../Source/TechnoXMidiControlRouter.h"
#include <cassert>
using namespace technox;
int main() {
    MidiControlRouter router;
    auto cc = router.handleControlChange(1, 7, 100);
    assert(cc && cc->parameter == PartParameter::level && cc->rawValue == 100);
    cc = router.handleControlChange(1, 10, 127);
    assert(cc && cc->parameter == PartParameter::panorama && cc->rawValue == 20);
    cc = router.handleControlChange(1, 64, 63);
    assert(cc && cc->rawValue == 0);
    cc = router.handleControlChange(1, 64, 64);
    assert(cc && cc->rawValue == 1);
    cc = router.handleControlChange(2, 0, 3);
    assert(cc && cc->parameter == PartParameter::bankNumber && cc->rawValue == 3);
    assert(router.bankSelectForChannel(2) == 3);
    assert(!router.handleControlChange(1, 0, 4));
    assert(!router.handleControlChange(1, 99, 1));
    assert(!router.handleControlChange(1, 98, 0x20));
    cc = router.handleControlChange(1, 6, 96);
    assert(cc && cc->parameter == PartParameter::cutoffFrequency && cc->rawValue == 96);
    assert(!router.handleControlChange(1, 99, 1));
    assert(!router.handleControlChange(1, 98, 0x10));
    cc = router.handleControlChange(1, 6, 32);
    assert(cc && cc->parameter == PartParameter::vibratoDelay && cc->rawValue == 32);
    assert(!router.handleControlChange(1, 99, 1));
    assert(!router.handleControlChange(1, 98, 0x63));
    cc = router.handleControlChange(1, 6, 48);
    assert(cc && cc->parameter == PartParameter::egAttack && cc->rawValue == 48);
    assert(!router.handleControlChange(1, 101, 0));
    assert(!router.handleControlChange(1, 100, 0));
    assert(!router.handleControlChange(1, 6, 64));
    assert(!router.handleControlChange(1, 99, 0x18));
    assert(!router.handleControlChange(1, 98, 0x1a));
    assert(!router.handleControlChange(1, 6, 100));
    assert(!router.handleControlChange(17, 7, 100));
    return 0;
}