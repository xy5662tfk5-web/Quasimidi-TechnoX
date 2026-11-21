#include "../Source/BasicMASSVoiceBackend.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

class CountingBackend final : public technox::IMASSVoiceBackend {
public:
    int noteOnCalls = 0;
    int noteOffCalls = 0;
    int resetVoiceCalls = 0;
    int prepareCalls = 0;
    int renderedCommonByte = -1;
    int renderedPartByte = -1;
    bool renderedImageLoaded = false;
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    void prepare(double sampleRate, int blockSize) override {
        ++prepareCalls;
        preparedSampleRate = sampleRate;
        preparedBlockSize = blockSize;
    }
    void noteOn(int, const technox::VoiceSlot&) noexcept override { ++noteOnCalls; }
    void noteOff(int, const technox::VoiceSlot&) noexcept override { ++noteOffCalls; }
    void resetVoice(int, const technox::VoiceSlot&) noexcept override { ++resetVoiceCalls; }
    bool isVoiceFinished(int) const noexcept override { return false; }
    void render(float* const*, int, int, const technox::VoiceManager&,
                const technox::ModulationMatrix&, const technox::MASSPartParameterStore&,
                const technox::MASSModulationSourceStore&, const technox::MASSPartRuntimeControlStore&,
                const technox::MASSCommonControlStore&,
                const technox::MASSPerformanceImageStore& image,
                const technox::MASSDrumParameterStore&) noexcept override {
        renderedCommonByte = image.commonByte(0);
        renderedPartByte = image.partByte(3, 23);
        renderedImageLoaded = image.isLoaded();
    }
};

int main() {
    using namespace technox;

    // Four Parts on distinct MIDI channels retain independent note lifecycles.
    CountingBackend fourPartBackend;
    MASSAudioEngine fourPartEngine(&fourPartBackend);
    fourPartEngine.prepare(48000.0, 128);
    for (int part = 0; part < 4; ++part)
        fourPartEngine.noteOn(static_cast<std::uint8_t>(12 + part),
                              static_cast<std::uint8_t>(13 + part),
                              static_cast<std::uint8_t>(60 + part), 100);
    assert(fourPartEngine.voiceManager().activeVoiceCount() == 4);
    fourPartEngine.noteOff(13, 60);
    int releasingParts = 0;
    for (const auto& voice : fourPartEngine.voiceManager().voices())
        if (voice.active && voice.releasing) {
            ++releasingParts;
            assert(voice.part == 12);
        }
    assert(releasingParts == 1);
    assert(fourPartEngine.voiceManager().activeVoiceCount() == 4);

    // Identical pitches on different Part channels must remain independent.
    fourPartEngine.noteOn(13, 14, 60, 100);
    fourPartEngine.noteOff(13, 60);
    int samePitchReleasing = 0;
    int samePitchHeld = 0;
    for (const auto& voice : fourPartEngine.voiceManager().voices()) {
        if (!voice.active || voice.note != 60) continue;
        if (voice.channel == 13 && voice.part == 12 && voice.releasing) ++samePitchReleasing;
        if (voice.channel == 14 && voice.part == 13 && !voice.releasing) ++samePitchHeld;
    }
    assert(samePitchReleasing == 1);
    assert(samePitchHeld == 1);

    // Channel-scoped All Notes Off and All Sounds Off must not touch another Part.
    fourPartEngine.allNotesOff(14);
    int channel13Releasing = 0;
    int channel14Releasing = 0;
    for (const auto& voice : fourPartEngine.voiceManager().voices()) {
        if (!voice.active) continue;
        if (voice.channel == 13 && voice.releasing) ++channel13Releasing;
        if (voice.channel == 14 && voice.releasing) ++channel14Releasing;
    }
    assert(channel13Releasing == 1);
    assert(channel14Releasing == 2);
    fourPartEngine.allSoundsOff(14);
    assert(fourPartEngine.voiceManager().activeVoiceCount() == 3);
    assert(fourPartBackend.resetVoiceCalls == 2);
    for (const auto& voice : fourPartEngine.voiceManager().voices())
        if (voice.active) assert(voice.channel != 14);

    // Modulation, RPN and switch controllers are stored independently per Part.
    assert(fourPartEngine.setModWheel(12, 90));
    assert(fourPartEngine.setModWheel(13, 12));
    assert(fourPartEngine.setExpression(12, 80));
    assert(fourPartEngine.setExpression(13, 30));
    assert(fourPartEngine.setPitchBend(12, 4096));
    assert(fourPartEngine.setPitchBend(13, -2048));
    assert(fourPartEngine.setRpn(12, 0, 12));
    assert(fourPartEngine.setRpn(13, 0, 2));
    assert(fourPartEngine.setPortamento(12, true));
    assert(fourPartEngine.setSoftPedal(13, true));
    assert(fourPartEngine.getModWheel(12) == 90 && fourPartEngine.getModWheel(13) == 12);
    assert(fourPartEngine.getExpression(12) == 80 && fourPartEngine.getExpression(13) == 30);
    assert(fourPartEngine.getPitchBend(12) == 4096 && fourPartEngine.getPitchBend(13) == -2048);
    assert(fourPartEngine.getRpn(12, 0) == 12 && fourPartEngine.getRpn(13, 0) == 2);
    assert(fourPartEngine.portamento(12) && !fourPartEngine.portamento(13));
    assert(!fourPartEngine.softPedal(12) && fourPartEngine.softPedal(13));
    fourPartEngine.resetPartControllers(12);
    assert(fourPartEngine.getModWheel(12) == 0 && fourPartEngine.getExpression(12) == 127);
    assert(fourPartEngine.getPitchBend(12) == 0 && fourPartEngine.getRpn(12, 0) == -1);
    assert(!fourPartEngine.portamento(12));
    assert(fourPartEngine.getModWheel(13) == 12 && fourPartEngine.getExpression(13) == 30);
    assert(fourPartEngine.getPitchBend(13) == -2048 && fourPartEngine.getRpn(13, 0) == 2);
    assert(fourPartEngine.softPedal(13));

    // With all 21 slots occupied, a releasing voice is stolen before a held
    // voice; the new note must occupy the released slot.
    CountingBackend stealingBackend;
    MASSAudioEngine stealingEngine(&stealingBackend);
    stealingEngine.prepare(48000.0, 128);
    for (int note = 0; note < 21; ++note)
        stealingEngine.noteOn(static_cast<std::uint8_t>(12 + (note % 4)),
                              static_cast<std::uint8_t>(1 + (note % 4)),
                              static_cast<std::uint8_t>(40 + note), 100);
    assert(stealingEngine.voiceManager().activeVoiceCount() == 21);
    stealingEngine.noteOff(1, 40);
    assert(stealingEngine.voiceManager().voices()[0].releasing);
    stealingEngine.noteOn(15, 4, 100, 110);
    assert(stealingEngine.voiceManager().activeVoiceCount() == 21);
    assert(stealingEngine.voiceManager().voices()[0].active);
    assert(!stealingEngine.voiceManager().voices()[0].releasing);
    assert(stealingEngine.voiceManager().voices()[0].note == 100);
    assert(stealingEngine.voiceManager().voices()[1].note == 41);
    assert(stealingBackend.noteOffCalls == 1);

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

    // Regression: full-scale Mod Wheel must produce a measurable vibrato
    // difference. This catches accidental double-normalization (127x too quiet).
    BasicMASSVoiceBackend noWheelBackend;
    BasicMASSVoiceBackend fullWheelBackend;
    MASSAudioEngine noWheelEngine(&noWheelBackend);
    MASSAudioEngine fullWheelEngine(&fullWheelBackend);
    constexpr int modulationSamples = 4096;
    noWheelEngine.prepare(48000.0, modulationSamples);
    fullWheelEngine.prepare(48000.0, modulationSamples);
    const std::array<MASSAudioEngine*, 2> candidates{{&noWheelEngine, &fullWheelEngine}};
    for (MASSAudioEngine* candidate : candidates) {
        assert(candidate->setPartParameter(12, PartParameter::level, 127));
        assert(candidate->setPartParameter(12, PartParameter::cutoffFrequency, 127));
        assert(candidate->setPartParameter(12, PartParameter::resonance, 0));
        assert(candidate->setPartParameter(12, PartParameter::egAttack, 0));
        assert(candidate->setPartParameter(12, PartParameter::egDecay, 0));
        assert(candidate->setPartParameter(12, PartParameter::egRelease, 0));
    }
    assert(noWheelEngine.setModWheel(12, 0));
    assert(fullWheelEngine.setModWheel(12, 127));
    noWheelEngine.noteOn(12, 1, 69, 110);
    fullWheelEngine.noteOn(12, 1, 69, 110);
    std::array<float, modulationSamples> noWheelLeft{}, noWheelRight{};
    std::array<float, modulationSamples> fullWheelLeft{}, fullWheelRight{};
    float* noWheelChannels[] = {noWheelLeft.data(), noWheelRight.data()};
    float* fullWheelChannels[] = {fullWheelLeft.data(), fullWheelRight.data()};
    noWheelEngine.render(noWheelChannels, 2, modulationSamples);
    fullWheelEngine.render(fullWheelChannels, 2, modulationSamples);
    double modulationDifference = 0.0;
    for (int i = 0; i < modulationSamples; ++i)
        modulationDifference += std::abs(noWheelLeft[static_cast<std::size_t>(i)] -
                                         fullWheelLeft[static_cast<std::size_t>(i)]);
    assert(modulationDifference > 0.01);

    // Backend hand-off: a recovered DSP can replace the prototype without
    // rebuilding MIDI/Part routing, and receives the active host format.
    CountingBackend prototypeBackend;
    CountingBackend replacementBackend;
    MASSAudioEngine swappableEngine(&prototypeBackend);
    swappableEngine.prepare(96000.0, 512);
    swappableEngine.noteOn(12, 1, 64, 100);
    assert(swappableEngine.voiceManager().activeVoiceCount() == 1);
    swappableEngine.setBackend(&replacementBackend);
    assert(prototypeBackend.resetVoiceCalls == 1);
    assert(swappableEngine.voiceManager().activeVoiceCount() == 0);
    assert(replacementBackend.prepareCalls == 1);
    assert(replacementBackend.preparedSampleRate == 96000.0);
    assert(replacementBackend.preparedBlockSize == 512);
    assert(swappableEngine.backend() == &replacementBackend);
    swappableEngine.noteOn(12, 1, 65, 100);
    assert(replacementBackend.noteOnCalls == 1);
    assert(swappableEngine.voiceManager().activeVoiceCount() == 1);

    // Recovered firmware image layout: 65 Common bytes + four 24-byte Parts.
    CountingBackend imageBackend;
    MASSAudioEngine imageEngine(&imageBackend);
    imageEngine.prepare(48000.0, 64);
    assert(imageEngine.setPerformanceCommonByte(0, 0x42));
    assert(imageEngine.setPerformancePartByte(3, 23, 0x65));
    imageEngine.markPerformanceImageLoaded();
    float imageSample = 0.0f;
    float* imageChannels[] = {&imageSample};
    imageEngine.render(imageChannels, 1, 1);
    assert(imageBackend.renderedImageLoaded);
    assert(imageBackend.renderedCommonByte == 0x42);
    assert(imageBackend.renderedPartByte == 0x65);
    assert(!imageEngine.setPerformanceCommonByte(65, 0));
    assert(!imageEngine.setPerformanceCommonByte(0, 128));
    assert(!imageEngine.setPerformancePartByte(4, 0, 0));
    assert(!imageEngine.setPerformancePartByte(0, 24, 0));
    assert(!imageEngine.setPerformancePartByte(0, 0, 128));
    imageEngine.clearPerformanceImage();
    assert(!imageEngine.performanceImage().isLoaded());

    std::cout << "Basic MASS backend: note-on, finite stereo output, note-off and release passed\n";
    return 0;
}
