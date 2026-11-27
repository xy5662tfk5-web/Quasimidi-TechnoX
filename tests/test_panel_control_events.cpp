#include "../Source/TechnoXPanelControlEvents.h"
#include <array>
#include <cassert>
#include <iostream>

int main()
{
    technox::PanelPartEditMailbox mailbox;
    using technox::PartParameter;
    using Edit = technox::PanelPartEditMailbox::Edit;

    int count = 0;

    // Repeated turns of one encoder coalesce to the latest raw value.
    assert(mailbox.publish(0, 0, PartParameter::level, 10));
    assert(mailbox.publish(0, 0, PartParameter::level, 42));
    mailbox.consume(0, [&](const Edit& edit) {
        ++count;
        assert(edit.partIndex == 0);
        assert(edit.parameter == PartParameter::level);
        assert(edit.rawValue == 42);
    });
    assert(count == 1);

    // PARAMETER selection is represented by a distinct parameter slot.
    assert(mailbox.publish(0, 0, PartParameter::cutoffFrequency, 77));
    mailbox.consume(0, [&](const Edit& edit) {
        ++count;
        assert(edit.partIndex == 0);
        assert(edit.parameter == PartParameter::cutoffFrequency);
        assert(edit.rawValue == 77);
    });
    assert(count == 2);

    // The same field in different Parts and Performances is independent.
    assert(mailbox.publish(1, 3, PartParameter::soundNumber, 127));
    assert(mailbox.publish(2, 3, PartParameter::soundNumber, 8));
    mailbox.consume(1, [&](const Edit& edit) {
        ++count;
        assert(edit.partIndex == 3);
        assert(edit.parameter == PartParameter::soundNumber);
        assert(edit.rawValue == 127);
    });
    mailbox.consume(2, [&](const Edit& edit) {
        ++count;
        assert(edit.partIndex == 3);
        assert(edit.parameter == PartParameter::soundNumber);
        assert(edit.rawValue == 8);
    });
    assert(count == 4);

    // Exercise the exact edit indices at word boundaries 31/32/63/64/95.
    const std::array<int, 5> partIndices{{1, 1, 2, 2, 3}};
    const std::array<PartParameter, 5> parameters{{
        PartParameter::transpose, PartParameter::tune,
        PartParameter::vibratoDepth, PartParameter::vibratoDelay,
        PartParameter::portamentoTime
    }};
    const std::array<int, 5> values{{33, 65, 92, 93, 127}};
    for (std::size_t i = 0; i < partIndices.size(); ++i)
        assert(mailbox.publish(4, partIndices[i], parameters[i], values[i]));

    std::array<bool, 5> seen{};
    mailbox.consume(4, [&](const Edit& edit) {
        for (std::size_t i = 0; i < partIndices.size(); ++i) {
            if (edit.partIndex == partIndices[i] && edit.parameter == parameters[i]) {
                assert(edit.rawValue == values[i]);
                seen[i] = true;
            }
        }
    });
    for (bool value : seen) assert(value);

    // Invalid addresses, parameters and out-of-range values are rejected.
    assert(!mailbox.publish(-1, 0, PartParameter::level, 1));
    assert(!mailbox.publish(50, 0, PartParameter::level, 1));
    assert(!mailbox.publish(0, -1, PartParameter::level, 1));
    assert(!mailbox.publish(0, 4, PartParameter::level, 1));
    assert(!mailbox.publish(0, 0, PartParameter::level, 128));
    assert(!mailbox.publish(0, 0, PartParameter::bankNumber, 4));

    // Clearing a selected Performance discards only that Performance's pending edits.
    assert(mailbox.publish(7, 2, PartParameter::level, 55));
    assert(mailbox.publish(8, 2, PartParameter::level, 66));
    mailbox.clear(7);
    mailbox.consume(7, [&](const Edit&) { assert(false); });
    bool preservedOtherPerformance = false;
    mailbox.consume(8, [&](const Edit& edit) {
        preservedOtherPerformance = true;
        assert(edit.rawValue == 66);
    });
    assert(preservedOtherPerformance);

    mailbox.consume(0, [&](const Edit&) { assert(false); });

    // Sequencer Multi owns a separate 16-Part mailbox. Verify Part 1, drum
    // Part 10 and Part 16 remain independent of factory Performance Parts.
    technox::PanelSequencerPartEditMailbox sequencerMailbox;
    assert(sequencerMailbox.publish(0, PartParameter::bankNumber, 0));
    assert(sequencerMailbox.publish(9, PartParameter::soundNumber, 7));
    assert(sequencerMailbox.publish(15, PartParameter::soundNumber, 127));
    std::array<bool, 3> multiSeen{};
    sequencerMailbox.consume([&](const technox::PanelSequencerPartEditMailbox::Edit& edit) {
        if (edit.partIndex == 0 && edit.parameter == PartParameter::bankNumber) {
            assert(edit.rawValue == 0);
            multiSeen[0] = true;
        } else if (edit.partIndex == 9 && edit.parameter == PartParameter::soundNumber) {
            assert(edit.rawValue == 7);
            multiSeen[1] = true;
        } else if (edit.partIndex == 15 && edit.parameter == PartParameter::soundNumber) {
            assert(edit.rawValue == 127);
            multiSeen[2] = true;
        } else {
            assert(false);
        }
    });
    for (bool seen : multiSeen) assert(seen);
    assert(!sequencerMailbox.publish(16, PartParameter::level, 1));

    std::cout << "TechnoX panel edit mailbox tests passed\\n";
    return 0;
}
