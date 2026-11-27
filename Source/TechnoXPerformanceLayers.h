#pragma once

namespace technox {

// IC4 dispatcher 0x593D reads the Common mode byte at XDATA 0x7BD5:
// values 0..3 select event IDs 0x0C..0x0F cumulatively (Single/Layer2/3/4).
// Slots 0..3 are the four stored Performance Parts 13..16. A Part with
// Track Mode 0 is muted regardless of the selected layer count.
constexpr bool isPerformanceLayerActive(int performanceMode, int partSlot, int trackMode) noexcept {
    return performanceMode >= 0 && performanceMode <= 3 &&
           partSlot >= 0 && partSlot < 4 &&
           partSlot <= performanceMode &&
           trackMode > 0 && trackMode <= 3;
}

} // namespace technox
