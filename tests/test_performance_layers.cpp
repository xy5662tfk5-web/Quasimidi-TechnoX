#include "../Source/TechnoXPerformanceLayers.h"
#include <cassert>
using technox::isPerformanceLayerActive;
int main() {
    assert(isPerformanceLayerActive(0, 0, 1));
    assert(!isPerformanceLayerActive(0, 1, 1));
    assert(isPerformanceLayerActive(1, 0, 1));
    assert(isPerformanceLayerActive(1, 1, 2));
    assert(!isPerformanceLayerActive(1, 2, 1));
    assert(isPerformanceLayerActive(2, 2, 3));
    assert(isPerformanceLayerActive(3, 3, 1));
    assert(!isPerformanceLayerActive(3, 3, 0));
    assert(!isPerformanceLayerActive(4, 0, 1));
    assert(!isPerformanceLayerActive(1, 4, 1));
    return 0;
}