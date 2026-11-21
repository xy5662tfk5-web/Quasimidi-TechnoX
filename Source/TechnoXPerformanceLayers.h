#pragma once

namespace technox {

// Performance mode 0..3 means Single/Layer2/Layer3/Layer4.
// The stored Performance Part slots are ordered as hardware Parts 13..16.
// A Part with Track Mode 0 is muted regardless of the selected layer count.
constexpr bool isPerformanceLayerActive(int performanceMode, int partSlot, int trackMode) noexcept {
    return performanceMode >= 0 && performanceMode <= 3 &&
           partSlot >= 0 && partSlot < 4 &&
           partSlot <= performanceMode &&
           trackMode > 0 && trackMode <= 3;
}

} // namespace technox
