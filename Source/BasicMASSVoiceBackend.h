#pragma once

#include "MASSAudioEngine.h"
#include "MASSOriginalVoiceProgramRuntime.h"
#include "MASSWaveformMemoryView.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace technox {

// First audible M.A.S.S. prototype. This is deliberately a generic test voice,
// not a claim that the TechnoX ROM/sample format has been decoded.
class BasicMASSVoiceBackend final : public IMASSVoiceBackend {
public:
    // These callbacks now bind the selected voice to the already-decoded
    // IC51 program stream and startup external-memory image. The renderer
    // below remains a diagnostic oscillator until the waveform format and
    // original DSP playback algorithm are verified.
    void setProgramCatalog(const MASSResourceProgramCatalog* catalog) noexcept override {
        originalProgramRuntime_.setProgramCatalog(catalog);
    }

    void setExternalSoundMemoryImage(
        const MASSIC51ExternalSoundMemoryImage* image) noexcept override {
        originalProgramRuntime_.setExternalSoundMemoryImage(image);
    }

    void setWaveformMemoryView(const MASSWaveformMemoryView* view) noexcept override {
        waveformMemoryView_ = view;
    }

    const MASSWaveformMemoryView* waveformMemoryViewForTesting() const noexcept {
        return waveformMemoryView_;
    }
    bool readWaveformMemoryByteForTesting(
        std::size_t address, std::uint8_t& value) const noexcept {
        return waveformMemoryView_ != nullptr && waveformMemoryView_->readByte(address, value);
    }

    const MASSOriginalVoiceProgramRuntime::VoiceProgramBinding*
    originalProgramBindingForTesting(int voiceIndex) const noexcept {
        return originalProgramRuntime_.bindingForVoice(voiceIndex);
    }

    const MASSResourceCommand* originalProgramCommandForTesting(
        int voiceIndex, bool firmwareFlag0ASet,
        std::size_t commandIndex) const noexcept {
        return originalProgramRuntime_.commandForVoice(
            voiceIndex, firmwareFlag0ASet, commandIndex);
    }

    bool readOriginalSoundMemoryForTesting(
        std::uint16_t address, std::uint8_t& value) const noexcept {
        return originalProgramRuntime_.readExternalSoundMemory(address, value);
    }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate > 8000.0 ? sampleRate : 44100.0;
        for (auto& voice : states_) voice = {};
        originalProgramRuntime_.reset();
    }

    void noteOn(int voiceIndex, const VoiceSlot& voice) noexcept override {
        if (!valid(voiceIndex)) return;
        originalProgramRuntime_.noteOn(voiceIndex, voice);
        auto& state = states_[static_cast<std::size_t>(voiceIndex)];
        state = {};
        state.active = true;
        state.gate = true;
        state.stage = Stage::attack;
        state.phase = 0.0;
        state.note = voice.note;
    }

    void noteOff(int voiceIndex, const VoiceSlot&) noexcept override {
        if (!valid(voiceIndex)) return;
        originalProgramRuntime_.noteOff(voiceIndex);
        auto& state = states_[static_cast<std::size_t>(voiceIndex)];
        if (state.active) {
            state.gate = false;
            if (state.stage != Stage::off && state.stage != Stage::release) {
                state.releaseStartEnvelope = state.envelope;
                state.stage = Stage::release;
            }
        }
    }

    void resetVoice(int voiceIndex, const VoiceSlot&) noexcept override {
        if (valid(voiceIndex)) {
            states_[static_cast<std::size_t>(voiceIndex)] = {};
            originalProgramRuntime_.resetVoice(voiceIndex);
        }
    }

    bool isVoiceFinished(int voiceIndex) const noexcept override {
        return !valid(voiceIndex) || !states_[static_cast<std::size_t>(voiceIndex)].active;
    }

    void render(float* const* channels, int channelCount, int sampleCount,
                const VoiceManager& voices, const ModulationMatrix& matrix,
                const MASSPartParameterStore& parts, const MASSModulationSourceStore& sources,
                const MASSPartRuntimeControlStore& runtimeControls, const MASSCommonControlStore&,
                const MASSPerformanceImageStore& performanceImage,
                const MASSDrumParameterStore&) noexcept override {
        if (channels == nullptr || channelCount <= 0 || sampleCount <= 0) return;

        // Common parameter 0 is the overall Performance level (0..127).
        // The prototype defaults to unity when no complete Performance image
        // has been loaded into the engine.
        const int rawPerformanceLevel = performanceImage.isLoaded()
            ? performanceImage.commonByte(0) : 127;
        const float performanceGain = rawPerformanceLevel < 0
            ? 1.0f : static_cast<float>(rawPerformanceLevel) / 127.0f;

        // Keep original IC51 program/resource bindings synchronized with the
        // authoritative voice slots. This covers legato retargeting and
        // program changes that do not issue a new backend noteOn callback.
        const auto& activeSlots = voices.voices();
        for (std::size_t index = 0; index < activeSlots.size(); ++index)
            originalProgramRuntime_.synchronizeVoice(
                static_cast<int>(index), activeSlots[index]);

        for (int sample = 0; sample < sampleCount; ++sample) {
            float mixedLeft = 0.0f;
            float mixedRight = 0.0f;
            const auto& slots = voices.voices();
            for (std::size_t index = 0; index < slots.size(); ++index) {
                const auto& slot = slots[index];
                auto& state = states_[index];
                if (!slot.active || !state.active) continue;

                const int part = static_cast<int>(slot.part);
                const int rawLevel = parts.get(part, PartParameter::level);
                const int rawTranspose = parts.get(part, PartParameter::transpose);
                const int rawTune = parts.get(part, PartParameter::tune);
                const int rawCutoff = parts.get(part, PartParameter::cutoffFrequency);
                const int rawResonance = parts.get(part, PartParameter::resonance);
                const int rawAttack = parts.get(part, PartParameter::egAttack);
                const int rawDecay = parts.get(part, PartParameter::egDecay);
                const int rawRelease = parts.get(part, PartParameter::egRelease);

                const float level = rawLevel < 0 ? 0.8f : static_cast<float>(rawLevel) / 127.0f;
                const float expression = static_cast<float>(sources.expression(part)) / 127.0f;
                const float modWheel = static_cast<float>(sources.modWheel(part)) / 127.0f;
                const float aftertouch = static_cast<float>(sources.aftertouch(part)) / 127.0f;
                const float pitchBendSource = juceSafeClamp(
                    static_cast<float>(sources.pitchBend(part)) / 8192.0f, -1.0f, 1.0f);
                const float freeController = static_cast<float>(sources.freeController(part)) / 127.0f;
                const float velocity = static_cast<float>(slot.velocity) / 127.0f;
                const float keyTrack = juceSafeClamp((static_cast<float>(slot.note) - 60.0f) / 60.0f, -1.0f, 1.0f);
                const float pitchModulation = matrix.contribution(
                    ModDestination::pitch, modWheel, velocity, keyTrack, aftertouch,
                    pitchBendSource, freeController);
                const float cutoffModulation = matrix.contribution(
                    ModDestination::cutoff, modWheel, velocity, keyTrack, aftertouch,
                    pitchBendSource, freeController);
                const float levelModulation = matrix.contribution(
                    ModDestination::level, modWheel, velocity, keyTrack, aftertouch,
                    pitchBendSource, freeController);
                const float panModulation = juceSafeClamp(matrix.contribution(
                    ModDestination::panorama, modWheel, velocity, keyTrack, aftertouch,
                    pitchBendSource, freeController), -1.0f, 1.0f);
                const float attack = timeSeconds(rawAttack, 0.002f, 1.5f);
                const float decay = timeSeconds(rawDecay, 0.01f, 2.0f);
                const float release = timeSeconds(rawRelease, 0.01f, 3.0f);
                updateEnvelope(state, attack, decay, release);

                const int bendRaw = sources.pitchBend(part);
                const int bendRangeSemitones = runtimeControls.rpn(part, 0) >= 0
                    ? runtimeControls.rpn(part, 0) : 2;
                const int bendRangeCents = runtimeControls.rpnLsb(part, 0) >= 0
                    ? runtimeControls.rpnLsb(part, 0) : 0;
                const double bendRange = static_cast<double>(bendRangeSemitones) +
                    static_cast<double>(bendRangeCents) / 100.0;
                const double bendSemitones = (static_cast<double>(bendRaw) / 8192.0) * bendRange;
                const double vibratoSemitones = static_cast<double>(modWheel) * 0.5 *
                    std::sin(state.lfoPhase);
                const double transposeSemitones = rawTranspose < 0
                    ? 0.0 : static_cast<double>(rawTranspose - 24);
                const double fineTuneSemitones = rawTune < 0
                    ? 0.0 : static_cast<double>(rawTune - 64) / 100.0;
                const double targetFrequency = 440.0 * std::pow(2.0,
                    (static_cast<double>(slot.note) - 69.0 + transposeSemitones + fineTuneSemitones +
                     bendSemitones + vibratoSemitones +
                     static_cast<double>(pitchModulation) * 12.0) / 12.0);
                const int trackMode = parts.get(part, PartParameter::trackMode);
                if (state.note != slot.note) {
                    state.note = slot.note;
                    state.gliding = trackMode == 3 && runtimeControls.portamento(part);
                }
                if (!state.frequencyInitialized) {
                    state.currentFrequency = targetFrequency;
                    state.frequencyInitialized = true;
                } else if (state.gliding && runtimeControls.portamento(part) && trackMode == 3) {
                    const int rawPortamento = parts.get(part, PartParameter::portamentoTime);
                    const double normalizedTime = rawPortamento < 0
                        ? 0.2 : static_cast<double>(rawPortamento) / 127.0;
                    const double glideSeconds = 0.005 * std::pow(200.0, normalizedTime);
                    const double coefficient = 1.0 - std::exp(-1.0 / (glideSeconds * sampleRate_));
                    state.currentFrequency += (targetFrequency - state.currentFrequency) * coefficient;
                    if (std::abs(targetFrequency - state.currentFrequency) < 0.05) {
                        state.currentFrequency = targetFrequency;
                        state.gliding = false;
                    }
                } else {
                    state.currentFrequency = targetFrequency;
                    state.gliding = false;
                }
                const double frequency = state.currentFrequency;
                state.lfoPhase += twoPi * 5.0 / sampleRate_;
                if (state.lfoPhase >= twoPi) state.lfoPhase -= twoPi;
                const float oscillator = static_cast<float>(std::sin(state.phase));
                state.phase += twoPi * frequency / sampleRate_;
                if (state.phase >= twoPi) state.phase -= twoPi;

                const float cutoffNorm = rawCutoff < 0 ? 0.78f : static_cast<float>(rawCutoff) / 127.0f;
                const float modulatedCutoff = juceSafeClamp(
                    cutoffNorm + modWheel * 0.35f + cutoffModulation, 0.0f, 1.0f);
                const float cutoffHz = 35.0f * std::pow(350.0f, modulatedCutoff);
                const float resonanceNorm = rawResonance < 0 ? 0.0f : static_cast<float>(rawResonance) / 127.0f;
                const float q = 0.707f + resonanceNorm * 9.0f;
                const float filtered = lowPass(state, oscillator, cutoffHz, q);
                const float modulatedLevel = juceSafeClamp(level + levelModulation, 0.0f, 1.0f);
                const float voiceSample = filtered * state.envelope * velocity *
                    modulatedLevel * expression * 0.18f;
                // The manual defines raw values 1..15 as seven steps left,
                // center and seven steps right. Value 0 (Off) and the special
                // Random/Key/Velocity modes are not synthesized in this basic
                // prototype; keep them centered rather than inventing behavior.
                const int rawPanorama = parts.get(part, PartParameter::panorama);
                const float staticPanorama = rawPanorama >= 1 && rawPanorama <= 15
                    ? static_cast<float>(rawPanorama - 8) / 7.0f : 0.0f;
                const float totalPanorama = juceSafeClamp(
                    staticPanorama + panModulation, -1.0f, 1.0f);
                const float leftGain = totalPanorama > 0.0f ? 1.0f - totalPanorama : 1.0f;
                const float rightGain = totalPanorama < 0.0f ? 1.0f + totalPanorama : 1.0f;
                mixedLeft += voiceSample * leftGain;
                mixedRight += voiceSample * rightGain;

                if (state.stage == Stage::off) state.active = false;
            }
            const float limitedLeft = std::tanh(mixedLeft) * performanceGain;
            const float limitedRight = std::tanh(mixedRight) * performanceGain;
            if (channelCount == 1) {
                channels[0][sample] += 0.5f * (limitedLeft + limitedRight);
            } else {
                channels[0][sample] += limitedLeft;
                channels[1][sample] += limitedRight;
                for (int channel = 2; channel < channelCount; ++channel)
                    channels[channel][sample] += 0.5f * (limitedLeft + limitedRight);
            }
        }
    }

private:
    enum class Stage : std::uint8_t { off, attack, decay, sustain, release };
    struct VoiceState {
        bool active = false;
        bool gate = false;
        Stage stage = Stage::off;
        double phase = 0.0;
        double currentFrequency = 0.0;
        double lfoPhase = 0.0;
        float envelope = 0.0f;
        float releaseStartEnvelope = 0.0f;
        float filterIc1 = 0.0f;
        float filterIc2 = 0.0f;
        std::uint8_t note = 0;
        bool frequencyInitialized = false;
        bool gliding = false;
    };

    static constexpr double twoPi = 6.28318530717958647692;
    std::array<VoiceState, VoiceManager::kVoiceCount> states_{};
    MASSOriginalVoiceProgramRuntime originalProgramRuntime_{};
    const MASSWaveformMemoryView* waveformMemoryView_ = nullptr;
    double sampleRate_ = 44100.0;

    static bool valid(int index) noexcept {
        return index >= 0 && index < static_cast<int>(VoiceManager::kVoiceCount);
    }

    static float timeSeconds(int raw, float minimum, float maximum) noexcept {
        const float normalized = raw < 0 ? 0.2f : static_cast<float>(raw) / 127.0f;
        return minimum * std::pow(maximum / minimum, normalized);
    }

    void updateEnvelope(VoiceState& state, float attack, float decay, float release) const noexcept {
        const float sr = static_cast<float>(sampleRate_);
        switch (state.stage) {
            case Stage::attack:
                state.envelope += 1.0f / (attack * sr);
                if (state.envelope >= 1.0f) {
                    state.envelope = 1.0f;
                    state.stage = Stage::decay;
                }
                break;
            case Stage::decay:
                state.envelope -= (1.0f - 0.72f) / (decay * sr);
                if (state.envelope <= 0.72f) {
                    state.envelope = 0.72f;
                    state.stage = state.gate ? Stage::sustain : Stage::release;
                }
                break;
            case Stage::sustain:
                if (!state.gate) state.stage = Stage::release;
                break;
            case Stage::release:
                // Release from the envelope's actual level at Note Off.
                // A fixed 0.72 decrement truncated notes released during attack.
                state.envelope -= state.releaseStartEnvelope / (release * sr);
                if (state.envelope <= 0.00001f) {
                    state.envelope = 0.0f;
                    state.stage = Stage::off;
                }
                break;
            case Stage::off:
                state.envelope = 0.0f;
                break;
        }
    }

    float lowPass(VoiceState& state, float input, float cutoffHz, float q) const noexcept {
        const float maximumCutoff = static_cast<float>(sampleRate_) * 0.45f;
        const float cutoff = juceSafeClamp(cutoffHz, 20.0f, maximumCutoff);
        const float g = std::tan(3.14159265358979323846f * cutoff / static_cast<float>(sampleRate_));
        const float k = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v1 = a1 * (state.filterIc1 + g * (input - state.filterIc2));
        const float v2 = state.filterIc2 + g * v1;
        state.filterIc1 = 2.0f * v1 - state.filterIc1;
        state.filterIc2 = 2.0f * v2 - state.filterIc2;
        return v2;
    }

    static float juceSafeClamp(float value, float low, float high) noexcept {
        return value < low ? low : (value > high ? high : value);
    }
};

} // namespace technox
