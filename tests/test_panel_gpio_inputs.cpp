#include "../Source/TechnoX8032Machine.h"
#include <cassert>
#include <cstdint>
#include <iostream>

int main()
{
    technox::TechnoX8032Machine machine;
    machine.reset();

    // The panel scan routines drive P1 but read external pin levels.
    // Inject P1.3/P1.5 independently of the output latch.
    machine.writeInternal(0x90u, 0xffu);
    machine.setExternalPortInputs(0x90u, 0x28u, 0x00u);
    auto p1 = machine.readInternal(0x90u);
    assert((p1 & 0x28u) == 0x00u);
    assert((p1 & 0xD7u) == 0xD7u);

    machine.setExternalPortInputs(0x90u, 0x28u, 0x28u);
    p1 = machine.readInternal(0x90u);
    assert((p1 & 0x28u) == 0x28u);
    assert((p1 & 0xD7u) == 0xD7u);

    // P3.0 is an independent scan signal and must be injectable separately.
    machine.writeInternal(0xB0u, 0xffu);
    machine.setExternalPortInputs(0xB0u, 0x01u, 0x00u);
    assert((machine.readInternal(0xB0u) & 0x01u) == 0x00u);
    machine.setExternalPortInputs(0xB0u, 0x01u, 0x01u);
    assert((machine.readInternal(0xB0u) & 0x01u) == 0x01u);

    // The input override does not replace the output latch or unrelated pins.
    machine.setExternalPortInputs(0x90u, 0x08u, 0x00u);
    machine.writeInternal(0x90u, 0x55u);
    p1 = machine.readInternal(0x90u);
    assert((p1 & 0x08u) == 0x00u);
    assert((p1 & 0xF7u) == (0x55u & 0xF7u));

    std::cout << "TechnoX front-panel GPIO input tests passed\\n";
    return 0;
}
