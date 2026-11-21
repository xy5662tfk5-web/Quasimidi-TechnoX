#include "../Source/BasicMASSVoiceBackend.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace technox;

    BasicMASSVoiceBackend backend;
    MASSAudioEngine engine(&backend);
    engine.prepare(48000.0, 512);

    // Set a clearly audible test patch. These are generic prototype mappings,
    // not claimed to be verified TechnoX DSP parameter laws.
    assert(engine.setPartParameter(12, PartParameter::level, 127));
    assert(engine.setPartParameter(12, PartParameter::cutoffFrequency, 127));
    assert(engine.setPartParameter(12, PartParameter::resonance, 0));
    assert(engine.setPartParameter(12, PartParameter::egAttack, 0));
    assert(engine.setPartParameter(12, PartParameter::egDecay, 0));
    assert(engine.setPartParameter(12, PartParameter::egRelease, 0));

    engine.noteOn(12, 1, 69, 110);
    assert(engine.voiceManager().activeVoiceCount() == 1);

    std::array<float, 512> left{};
    std::array<float, 512> right{};
    float* channels[] = {left.data(), right.data()};
    engine.render(channels, 2, static_cast<int>(left.size()));

    double energy = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        assert(std::isfinite(left[i]));
        assert(std::isfinite(right[i]));
        energy += std::abs(left[i]) + std::abs(right[i]);
    }
    assert(energy > 0.01);

    engine.noteOff(1, 69);
    assert(engine.voiceManager().voices()[0].releasing);

    // Render long enough for the shortest configured release to finish.
    for (int block = 0; block < 20; ++block) {
        left.fill(0.0f);
        right.fill(0.0f);
        engine.render(channels, 2, static_cast<int>(left.size()));
    }
    assert(engine.voiceManager().activeVoiceCount() == 0);

    std::cout << "Basic MASS backend: note-on, finite stereo output, note-off and release passed\n";
    return 0;
}
