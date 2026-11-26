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
    int renderedNameByte = -1;
    int renderedCommonByte = -1;
    int renderedPartByte = -1;
    bool renderedImageLoaded = false;
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    const technox::MASSWaveformMemoryView* waveformMemoryView = nullptr;
    void setWaveformMemoryView(
        const technox::MASSWaveformMemoryView* view) noexcept override {
        waveformMemoryView = view;
    }
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
        renderedNameByte = image.nameByte(0);
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

    // Channel pressure can drive the modulation matrix without changing the
    // default patch when no route is configured.
    BasicMASSVoiceBackend noPressureBackend;
    BasicMASSVoiceBackend pressureBackend;
    MASSAudioEngine noPressureEngine(&noPressureBackend);
    MASSAudioEngine pressureEngine(&pressureBackend);
    noPressureEngine.prepare(48000.0, modulationSamples);
    pressureEngine.prepare(48000.0, modulationSamples);
    for (MASSAudioEngine* candidate : {&noPressureEngine, &pressureEngine}) {
        assert(candidate->setPartParameter(12, PartParameter::level, 127));
        assert(candidate->setPartParameter(12, PartParameter::cutoffFrequency, 100));
        assert(candidate->setPartParameter(12, PartParameter::resonance, 0));
        assert(candidate->setPartParameter(12, PartParameter::egAttack, 0));
        assert(candidate->setPartParameter(12, PartParameter::egDecay, 0));
        assert(candidate->setPartParameter(12, PartParameter::egRelease, 0));
    }
    assert(pressureEngine.setAftertouch(12, 127));
    assert(pressureEngine.modulationMatrix().setRoute(
        0, ModSource::aftertouch, ModDestination::pitch, 0.5f));
    noPressureEngine.noteOn(12, 1, 69, 110);
    pressureEngine.noteOn(12, 1, 69, 110);
    std::array<float, modulationSamples> noPressureLeft{}, noPressureRight{};
    std::array<float, modulationSamples> pressureLeft{}, pressureRight{};
    float* noPressureChannels[] = {noPressureLeft.data(), noPressureRight.data()};
    float* pressureChannels[] = {pressureLeft.data(), pressureRight.data()};
    noPressureEngine.render(noPressureChannels, 2, modulationSamples);
    pressureEngine.render(pressureChannels, 2, modulationSamples);
    double pressureDifference = 0.0;
    for (int i = 0; i < modulationSamples; ++i)
        pressureDifference += std::abs(noPressureLeft[static_cast<std::size_t>(i)] -
                                       pressureLeft[static_cast<std::size_t>(i)]);
    assert(pressureDifference > 0.01);

    // Pitch Bend and the performance-selected Free Controller are independent
    // modulation sources; when explicitly routed, both affect the test voice.
    BasicMASSVoiceBackend noFreeBackend, freeBackend;
    MASSAudioEngine noFreeEngine(&noFreeBackend), freeEngine(&freeBackend);
    noFreeEngine.prepare(48000.0, modulationSamples);
    freeEngine.prepare(48000.0, modulationSamples);
    for (MASSAudioEngine* candidate : {&noFreeEngine, &freeEngine}) {
        assert(candidate->setPartParameter(12, PartParameter::level, 127));
        assert(candidate->setPartParameter(12, PartParameter::cutoffFrequency, 100));
        assert(candidate->setPartParameter(12, PartParameter::resonance, 0));
        assert(candidate->setPartParameter(12, PartParameter::egAttack, 0));
        assert(candidate->setPartParameter(12, PartParameter::egDecay, 0));
        assert(candidate->setPartParameter(12, PartParameter::egRelease, 0));
    }
    assert(freeEngine.setFreeController(12, 127));
    assert(freeEngine.modulationMatrix().setRoute(
        0, ModSource::freeController, ModDestination::pitch, 0.35f));
    noFreeEngine.noteOn(12, 1, 69, 110);
    freeEngine.noteOn(12, 1, 69, 110);
    std::array<float, modulationSamples> noFreeLeft{}, noFreeRight{}, freeLeft{}, freeRight{};
    float* noFreeChannels[] = {noFreeLeft.data(), noFreeRight.data()};
    float* freeChannels[] = {freeLeft.data(), freeRight.data()};
    noFreeEngine.render(noFreeChannels, 2, modulationSamples);
    freeEngine.render(freeChannels, 2, modulationSamples);
    double freeDifference = 0.0;
    for (int i = 0; i < modulationSamples; ++i)
        freeDifference += std::abs(noFreeLeft[static_cast<std::size_t>(i)] -
                                   freeLeft[static_cast<std::size_t>(i)]);
    assert(freeDifference > 0.01);

    // Verified manual panorama positions 1..15 range from hard-left to hard-right.
    auto measurePanEnergy = [](int panorama, double& leftEnergy, double& rightEnergy) {
        BasicMASSVoiceBackend panBackend;
        MASSAudioEngine panEngine(&panBackend);
        constexpr int panSamples = 2048;
        panEngine.prepare(48000.0, panSamples);
        assert(panEngine.setPartParameter(12, PartParameter::level, 127));
        assert(panEngine.setPartParameter(12, PartParameter::cutoffFrequency, 127));
        assert(panEngine.setPartParameter(12, PartParameter::resonance, 0));
        assert(panEngine.setPartParameter(12, PartParameter::egAttack, 0));
        assert(panEngine.setPartParameter(12, PartParameter::egDecay, 0));
        assert(panEngine.setPartParameter(12, PartParameter::egRelease, 0));
        assert(panEngine.setPartParameter(12, PartParameter::panorama, panorama));
        panEngine.noteOn(12, 1, 69, 127);
        std::array<float, panSamples> left{}, right{};
        float* channels[] = {left.data(), right.data()};
        panEngine.render(channels, 2, panSamples);
        leftEnergy = rightEnergy = 0.0;
        for (int i = 0; i < panSamples; ++i) {
            leftEnergy += std::abs(left[static_cast<std::size_t>(i)]);
            rightEnergy += std::abs(right[static_cast<std::size_t>(i)]);
        }
    };
    double hardLeftL = 0.0, hardLeftR = 0.0, hardRightL = 0.0, hardRightR = 0.0;
    measurePanEnergy(1, hardLeftL, hardLeftR);
    measurePanEnergy(15, hardRightL, hardRightR);
    assert(hardLeftL > 0.01 && hardLeftR == 0.0);
    assert(hardRightR > 0.01 && hardRightL == 0.0);

    // Backend hand-off: a recovered DSP can replace the prototype without
    // rebuilding MIDI/Part routing, and receives the active host format.
    CountingBackend prototypeBackend;
    CountingBackend replacementBackend;
    MASSAudioEngine swappableEngine(&prototypeBackend);
    swappableEngine.prepare(96000.0, 512);
    constexpr std::array<std::uint8_t, 6> waveformBytes{{0x00, 0x80, 0xFF, 0x7F, 0x22, 0x44}};
    MASSWaveformMemoryView waveformView;
    assert(waveformView.attach(waveformBytes.data(), waveformBytes.size()));
    swappableEngine.setWaveformMemoryView(&waveformView);
    assert(prototypeBackend.waveformMemoryView == &waveformView);
    swappableEngine.noteOn(12, 1, 64, 100);
    assert(swappableEngine.voiceManager().activeVoiceCount() == 1);
    swappableEngine.setBackend(&replacementBackend);
    assert(prototypeBackend.resetVoiceCalls == 1);
    assert(swappableEngine.voiceManager().activeVoiceCount() == 0);
    assert(replacementBackend.prepareCalls == 1);
    assert(replacementBackend.preparedSampleRate == 96000.0);
    assert(replacementBackend.preparedBlockSize == 512);
    assert(replacementBackend.waveformMemoryView == &waveformView);
    assert(swappableEngine.waveformMemoryView() == &waveformView);
    assert(swappableEngine.backend() == &replacementBackend);
    swappableEngine.noteOn(12, 1, 65, 100);
    assert(replacementBackend.noteOnCalls == 1);
    std::uint8_t forwardedWaveByte = 0;
    assert(waveformView.readByte(2, forwardedWaveByte) && forwardedWaveByte == 0xFF);
    swappableEngine.setWaveformMemoryView(nullptr);
    assert(replacementBackend.waveformMemoryView == nullptr);
    assert(swappableEngine.voiceManager().activeVoiceCount() == 1);

    // Common parameter 0 is the saved overall Performance output level.
    BasicMASSVoiceBackend performanceLevelBackend;
    MASSAudioEngine performanceLevelEngine(&performanceLevelBackend);
    constexpr int performanceSamples = 512;
    performanceLevelEngine.prepare(48000.0, performanceSamples);
    assert(performanceLevelEngine.setPartParameter(12, PartParameter::level, 127));
    assert(performanceLevelEngine.setPartParameter(12, PartParameter::cutoffFrequency, 127));
    assert(performanceLevelEngine.setPartParameter(12, PartParameter::resonance, 0));
    assert(performanceLevelEngine.setPartParameter(12, PartParameter::egAttack, 0));
    assert(performanceLevelEngine.setPartParameter(12, PartParameter::egDecay, 0));
    assert(performanceLevelEngine.setPartParameter(12, PartParameter::egRelease, 0));
    assert(performanceLevelEngine.setPerformanceCommonByte(0, 0));
    performanceLevelEngine.markPerformanceImageLoaded();
    performanceLevelEngine.noteOn(12, 1, 69, 127);
    std::array<float, performanceSamples> performanceLeft{}, performanceRight{};
    float* performanceChannels[] = {performanceLeft.data(), performanceRight.data()};
    performanceLevelEngine.render(performanceChannels, 2, performanceSamples);
    double mutedPerformanceEnergy = 0.0;
    for (int i = 0; i < performanceSamples; ++i)
        mutedPerformanceEnergy += std::abs(performanceLeft[static_cast<std::size_t>(i)]) +
                                  std::abs(performanceRight[static_cast<std::size_t>(i)]);
    assert(mutedPerformanceEnergy == 0.0);
    assert(performanceLevelEngine.setPerformanceCommonByte(0, 127));
    performanceLeft.fill(0.0f);
    performanceRight.fill(0.0f);
    performanceLevelEngine.render(performanceChannels, 2, performanceSamples);
    double fullPerformanceEnergy = 0.0;
    for (int i = 0; i < performanceSamples; ++i)
        fullPerformanceEnergy += std::abs(performanceLeft[static_cast<std::size_t>(i)]) +
                                 std::abs(performanceRight[static_cast<std::size_t>(i)]);
    assert(fullPerformanceEnergy > 0.01);

    // Part transpose (0..48, center 24) and tuning (0..127, center 64)
    // must alter oscillator frequency, not just remain editor metadata.
    constexpr int pitchSamples = 4096;
    auto renderPitch = [](int transposeRaw, int tuneRaw,
                          std::array<float, pitchSamples>& left,
                          std::array<float, pitchSamples>& right) {
        BasicMASSVoiceBackend pitchBackend;
        MASSAudioEngine pitchEngine(&pitchBackend);
        pitchEngine.prepare(48000.0, pitchSamples);
        assert(pitchEngine.setPartParameter(12, PartParameter::level, 127));
        assert(pitchEngine.setPartParameter(12, PartParameter::cutoffFrequency, 127));
        assert(pitchEngine.setPartParameter(12, PartParameter::resonance, 0));
        assert(pitchEngine.setPartParameter(12, PartParameter::egAttack, 0));
        assert(pitchEngine.setPartParameter(12, PartParameter::egDecay, 0));
        assert(pitchEngine.setPartParameter(12, PartParameter::egRelease, 0));
        assert(pitchEngine.setPartParameter(12, PartParameter::transpose, transposeRaw));
        assert(pitchEngine.setPartParameter(12, PartParameter::tune, tuneRaw));
        pitchEngine.noteOn(12, 1, 69, 127);
        float* channels[] = {left.data(), right.data()};
        pitchEngine.render(channels, 2, pitchSamples);
    };
    std::array<float, pitchSamples> basePitchL{}, basePitchR{}, octavePitchL{}, octavePitchR{},
        fineTunePitchL{}, fineTunePitchR{};
    renderPitch(24, 64, basePitchL, basePitchR);
    renderPitch(36, 64, octavePitchL, octavePitchR);
    renderPitch(24, 127, fineTunePitchL, fineTunePitchR);
    double octaveDifference = 0.0, fineTuneDifference = 0.0;
    for (int i = 0; i < pitchSamples; ++i) {
        octaveDifference += std::abs(basePitchL[static_cast<std::size_t>(i)] -
                                     octavePitchL[static_cast<std::size_t>(i)]);
        fineTuneDifference += std::abs(basePitchL[static_cast<std::size_t>(i)] -
                                       fineTunePitchL[static_cast<std::size_t>(i)]);
    }
    assert(octaveDifference > 1.0);
    assert(fineTuneDifference > 0.01);

    // Lead mode + Portamento CC65 glides from the previous pitch instead of
    // jumping immediately to the next note. Portamento Time controls duration.
    BasicMASSVoiceBackend glideBackend;
    MASSAudioEngine glideEngine(&glideBackend);
    constexpr int glideBlock = 8192;
    glideEngine.prepare(48000.0, glideBlock);
    assert(glideEngine.setPartParameter(12, PartParameter::level, 127));
    assert(glideEngine.setPartParameter(12, PartParameter::cutoffFrequency, 127));
    assert(glideEngine.setPartParameter(12, PartParameter::resonance, 0));
    assert(glideEngine.setPartParameter(12, PartParameter::egAttack, 0));
    assert(glideEngine.setPartParameter(12, PartParameter::egDecay, 0));
    assert(glideEngine.setPartParameter(12, PartParameter::egRelease, 0));
    assert(glideEngine.setPartParameter(12, PartParameter::trackMode, 3));
    assert(glideEngine.setPartParameter(12, PartParameter::portamentoTime, 127));
    assert(glideEngine.setPortamento(12, true));
    glideEngine.noteOn(12, 1, 69, 127);
    std::array<float, 1024> glideWarmupL{}, glideWarmupR{};
    float* warmupChannels[] = {glideWarmupL.data(), glideWarmupR.data()};
    glideEngine.render(warmupChannels, 2, 1024);
    glideEngine.noteOn(12, 1, 81, 127);
    std::array<float, glideBlock> glideLeft{}, glideRight{};
    float* glideChannels[] = {glideLeft.data(), glideRight.data()};
    glideEngine.render(glideChannels, 2, glideBlock);
    auto zeroCrossings = [](const float* signal, int start, int count) {
        int crossings = 0;
        for (int i = start + 1; i < start + count; ++i)
            if ((signal[i - 1] <= 0.0f && signal[i] > 0.0f) ||
                (signal[i - 1] >= 0.0f && signal[i] < 0.0f))
                ++crossings;
        return crossings;
    };
    const int earlyGlideCrossings = zeroCrossings(glideLeft.data(), 0, 2048);
    const int lateGlideCrossings = zeroCrossings(glideLeft.data(), 6144, 2048);
    assert(earlyGlideCrossings > 0);
    assert(lateGlideCrossings > earlyGlideCrossings);

    // Recovered firmware image layout: 8-byte name + 65 Common + four 24-byte Parts.
    CountingBackend imageBackend;
    MASSAudioEngine imageEngine(&imageBackend);
    imageEngine.prepare(48000.0, 64);
    assert(imageEngine.setPerformanceNameByte(0, 'T'));
    assert(imageEngine.setPerformanceCommonByte(0, 0x42));
    assert(imageEngine.setPerformancePartByte(3, 23, 0x65));
    imageEngine.markPerformanceImageLoaded();
    float imageSample = 0.0f;
    float* imageChannels[] = {&imageSample};
    imageEngine.render(imageChannels, 1, 1);
    assert(imageBackend.renderedImageLoaded);
    assert(imageBackend.renderedNameByte == 'T');
    assert(imageBackend.renderedCommonByte == 0x42);
    assert(imageBackend.renderedPartByte == 0x65);
    std::array<std::uint8_t, MASSPerformanceImageStore::kSerializedSize> packedImage{};
    assert(MASSPerformanceImageStore::kSerializedSize == 169);
    assert(imageEngine.performanceImage().copyTo(packedImage));
    assert(packedImage[0] == 'T');
    assert(packedImage[8] == 0x42);
    assert(packedImage[8 + 65 + 3 * 24 + 23] == 0x65);
    assert(!imageEngine.setPerformanceNameByte(8, 0));
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
