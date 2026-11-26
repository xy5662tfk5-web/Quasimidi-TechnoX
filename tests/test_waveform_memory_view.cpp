#include "../Source/MASSWaveformMemoryView.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

int main() {
    using technox::MASSWaveformMemoryView;

    constexpr std::array<std::uint8_t, 12> firstImage{{
        0x00, 0x12, 0x34, 0x56, 0x78, 0x9A,
        0xBC, 0xDE, 0xF0, 0x11, 0x22, 0x33
    }};
    constexpr std::array<std::uint8_t, 4> secondImage{{
        0xA5, 0x5A, 0xC3, 0x3C
    }};

    MASSWaveformMemoryView view;
    assert(!view.isAttached());
    assert(view.sizeInBytes() == 0);
    assert(view.data() == nullptr);

    assert(!view.attach(nullptr, firstImage.size()));
    assert(!view.isAttached());
    assert(!view.attach(firstImage.data(), 0));
    assert(!view.isAttached());

    assert(view.attach(firstImage.data(), firstImage.size()));
    assert(view.isAttached());
    assert(view.sizeInBytes() == firstImage.size());
    assert(view.data() == firstImage.data());

    std::uint8_t value = 0xFF;
    assert(view.readByte(0, value) && value == 0x00);
    assert(view.readByte(11, value) && value == 0x33);
    assert(!view.readByte(12, value));

    const auto* middle = view.dataAt(3, 4);
    assert(middle == firstImage.data() + 3);
    assert(middle[0] == 0x56 && middle[3] == 0xBC);
    assert(view.dataAt(firstImage.size(), 0) == firstImage.data() + firstImage.size());
    assert(view.dataAt(firstImage.size(), 1) == nullptr);
    assert(view.dataAt(11, 2) == nullptr);
    assert(view.dataAt(static_cast<std::size_t>(-1), 2) == nullptr);

    assert(view.attach(secondImage.data(), secondImage.size()));
    assert(view.sizeInBytes() == secondImage.size());
    assert(view.data() == secondImage.data());
    assert(view.readByte(0, value) && value == 0xA5);
    assert(!view.readByte(secondImage.size(), value));

    view.detach();
    assert(!view.isAttached());
    assert(view.sizeInBytes() == 0);
    assert(view.data() == nullptr);
    assert(view.dataAt(0, 0) == nullptr);
    assert(!view.readByte(0, value));

    std::cout << "Waveform memory view: attach, bounds, spans and detach passed\n";
    return 0;
}
