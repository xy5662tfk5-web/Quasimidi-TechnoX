#include "../Source/BasicMASSVoiceBackend.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

class CountingBackend final : public technox::IMASSVoiceBackend {
public:
    int noteOffCalls = 0;
    void prepare(double, int) override {}
    void noteOn(int, const technox::VoiceSlot&) noexcept override {}
    void noteOff(int, const technox::VoiceSlot&) noexcept override { ++noteOffCalls; }
    void resetVoice(int, const technox::VoiceSlot&) noexcept override {}
    bool isVoiceFinished(int) const noexcept override { return false; }
    void render(float* const*, int, int, const technox::VoiceManager&,
                const technox::ModulationMatrix&, const technox::MASSPartParameterStore&,
                const technox::MASSModulationSourceStore&, const technox::MASSPartRuntimeControlStore&,
                const technox::MASSCommonControlStore&, const technox::MASSDrumParameterStore&) noexcept override {}
};

int main() {
    using namespace technox;

    // Duplicate Note Off messages must not restart the backend release.
    CountingBackend countingBackend;
    MASSAudioEngine countingEngine(&countingBackend);
    countingEngine.prepare(48000.0, 128);
    countingEngine.noteOn(12, 1, 64, 100);
    countingEngine.noteOff(1, 64);
    countingEngine.noteOff(1, 64);
    assert(countingBackend.noteOffCalls == 1);
    countingEngine.noteOn(13, 2, 65, 100);
    countingEngine.allNotesOff(2);
    countingEngine.allNotesOff(2);
    assert(countingBackend.noteOffCalls == 2);

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

    // A MIDI event placed inside a host block must only affect the samples
    // after its event offset. This mirrors the processor's segmented rendering.
    BasicMASSVoiceBackend offsetBackend;
    MASSAudioEngine offsetEngine(&offsetBackend);
    offsetEngine.prepare(48000.0, 512);
    assert(offsetEngine.setPartParameter(12, PartParameter::level, 127));
    assert(offsetEngine.setPartParameter(12, PartParameter::cutoffFrequency, 127));
    assert(offsetEngine.setPartParameter(12, PartParameter::resonance, 0));
    assert(offsetEngine.setPartParameter(12, PartParameter::egAttack, 0));
    assert(offsetEngine.setPartParameter(12, PartParameter::egDecay, 0));
    assert(offsetEngine.setPartParameter(12, PartParameter::egRelease, 0));

    std::array<float, 512> offsetLeft{};
    std::array<float, 512> offsetRight{};
    float* offsetChannels[] = {offsetLeft.data(), offsetRight.data()};
    constexpr int noteOnOffset = 96;
    offsetEngine.render(offsetChannels, 2, noteOnOffset);
    offsetEngine.noteOn(12, 1, 69, 110);
    float* afterNoteOn[] = {offsetLeft.data() + noteOnOffset, offsetRight.data() + noteOnOffset};
    offsetEngine.render(afterNoteOn, 2, 512 - noteOnOffset);

    double preEventEnergy = 0.0;
    double postEventEnergy = 0.0;
    for (int i = 0; i < 512; ++i) {
        const double sampleEnergy = std::abs(offsetLeft[static_cast<std::size_t>(i)]) +
                                    std::abs(offsetRight[static_cast<std::size_t>(i)]);
        if (i < noteOnOffset)
            preEventEnergy += sampleEnergy;
        else
            postEventEnergy += sampleEnergy;
    }
    assert(preEventEnergy == 0.0);
    assert(postEventEnergy > 0.01);

    // Reinitializing the audio device must not leave stale active voice slots.
    assert(offsetEngine.voiceManager().activeVoiceCount() == 1);
    offsetEngine.prepare(44100.0, 256);
    assert(offsetEngine.voiceManager().activeVoiceCount() == 0);
    offsetEngine.noteOn(12, 1, 69, 110);
    assert(offsetEngine.voiceManager().activeVoiceCount() == 1);

    // Releasing during the attack must fade from the current envelope level,
    // rather than subtracting a fixed sustain-level amount and cutting off.
    BasicMASSVoiceBackend attackReleaseBackend;
    MASSAudioEngine attackReleaseEngine(&attackReleaseBackend);
    attackReleaseEngine.prepare(48000.0, 256);
    assert(attackReleaseEngine.setPartParameter(12, PartParameter::level, 127));
    assert(attackReleaseEngine.setPartParameter(12, PartParameter::cutoffFrequency, 127));
    assert(attackReleaseEngine.setPartParameter(12, PartParameter::egAttack, 127));
    assert(attackReleaseEngine.setPartParameter(12, PartParameter::egRelease, 0));
    std::array<float, 256> attackLeft{};
    std::array<float, 256> attackRight{};
    float* attackChannels[] = {attackLeft.data(), attackRight.data()};
    attackReleaseEngine.noteOn(12, 1, 60, 127);
    attackReleaseEngine.render(attackChannels, 2, 128);
    attackReleaseEngine.noteOff(1, 60);
    float* releaseChannels[] = {attackLeft.data() + 128, attackRight.data() + 128};
    attackReleaseEngine.render(releaseChannels, 2, 128);
    double earlyReleaseEnergy = 0.0;
    for (int i = 128; i < 256; ++i)
        earlyReleaseEnergy += std::abs(attackLeft[static_cast<std::size_t>(i)]) +
                              std::abs(attackRight[static_cast<std::size_t>(i)]);
    assert(earlyReleaseEnergy > 0.0);
    assert(attackReleaseEngine.voiceManager().activeVoiceCount() == 1);

    // MIDI Note On with velocity zero must release the backend envelope,
    // not merely mark the voice-manager slot as releasing.
    engine.noteOn(12, 1, 69, 110);
    assert(engine.voiceManager().activeVoiceCount() == 1);
    engine.noteOn(12, 1, 69, 0);
    assert(engine.voiceManager().voices()[0].releasing);

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
