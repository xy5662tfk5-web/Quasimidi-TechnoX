#pragma once

#include "MASSAudioEngine.h"
#include "MASSOriginalVoiceProgramRuntime.h"
#include "MASSWaveformMemoryView.h"
#include "SAM9407ExternalMemoryBus.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace technox {

// SAM9407-oriented sample voice model.
//
// This models the documented 16-bit sample-memory / per-voice playback domain.
// It is NOT a claim that the TechnoX IC4 four-byte port has been mapped to
// SAM9407 registers, nor that the fitted chip has been physically confirmed.
// Sample addresses and byte order must come from a verified TechnoX dump/map.
enum class SAM9407SampleByteOrder : std::uint8_t {
    littleEndian,
    bigEndian
};

enum class SAM9407LoopMode : std::uint8_t {
    oneShot,
    forward,
    alternating
};

struct SAM9407SampleMapEntry {
    std::uint8_t bank = 0;
    std::uint8_t program = 0;
    // Optional key/velocity region; legacy entries cover the full MIDI range.
    std::uint8_t keyLow = 0;
    std::uint8_t keyHigh = 127;
    std::uint8_t velocityLow = 0;
    std::uint8_t velocityHigh = 127;
    std::uint32_t startWord = 0;
    std::uint32_t endWord = 0;       // Exclusive absolute 16-bit word address.
    std::uint32_t loopStartWord = 0; // Absolute 16-bit word address.
    std::uint32_t loopEndWord = 0;   // Exclusive absolute 16-bit word address.
    std::uint32_t sampleRate = 44100;
    std::int16_t tuneCents = 0;
    std::uint8_t rootNote = 60;
    std::uint8_t gain = 127;
    std::uint8_t pan = 64;
    SAM9407SampleByteOrder byteOrder = SAM9407SampleByteOrder::littleEndian;
    SAM9407LoopMode loopMode = SAM9407LoopMode::oneShot;
};

class SAM9407VoiceBackend final : public IMASSVoiceBackend {
public:
    static constexpr std::size_t kMaximumSampleMappings = 2048;
    static constexpr std::uint32_t kMaximumWordAddress = 0x01ffffffu;

    bool addSampleMapping(const SAM9407SampleMapEntry& entry) noexcept {
        if (entry.startWord >= entry.endWord ||
            static_cast<std::uint64_t>(entry.endWord) >
                SAM9407ExternalMemoryBus::kAddressSpaceWordCount ||
            entry.sampleRate < 4000u || entry.sampleRate > 192000u ||
            entry.rootNote > 127u || entry.gain > 127u || entry.pan > 127u ||
            entry.keyLow > entry.keyHigh || entry.velocityLow > entry.velocityHigh ||
            entry.tuneCents < -1200 || entry.tuneCents > 1200)
            return false;

        if (entry.loopMode != SAM9407LoopMode::oneShot &&
            (entry.loopStartWord < entry.startWord ||
             entry.loopStartWord >= entry.loopEndWord ||
             entry.loopEndWord > entry.endWord))
            return false;

        bool replaced = false;
        for (std::size_t i = 0; i < sampleMapCount_; ++i) {
            if (sampleMap_[i].bank == entry.bank &&
                sampleMap_[i].program == entry.program &&
                sampleMap_[i].keyLow == entry.keyLow &&
                sampleMap_[i].keyHigh == entry.keyHigh &&
                sampleMap_[i].velocityLow == entry.velocityLow &&
                sampleMap_[i].velocityHigh == entry.velocityHigh) {
                sampleMap_[i] = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            if (sampleMapCount_ >= sampleMap_.size())
                return false;
            sampleMap_[sampleMapCount_++] = entry;
        }

        // Map files are loaded while audio is stopped. Invalidate matching
        // live voices so a previously missing/replaced entry is rebound on
        // the next render instead of leaving that voice silent/stale.
        for (auto& state : voices_) {
            if (state.bank == entry.bank && state.program == entry.program)
                state.mappingChecked = false;
        }
        return true;
    }

    void clearSampleMappings() noexcept {
        sampleMapCount_ = 0;
        for (auto& state : voices_)
            state = {};
    }

    std::size_t sampleMappingCount() const noexcept { return sampleMapCount_; }

    void setProgramCatalog(const MASSResourceProgramCatalog* catalog) noexcept override {
        programRuntime_.setProgramCatalog(catalog);
    }

    void setExternalSoundMemoryImage(
        const MASSIC51ExternalSoundMemoryImage* image) noexcept override {
        programRuntime_.setExternalSoundMemoryImage(image);
    }

    void setWaveformMemoryView(const MASSWaveformMemoryView* view) noexcept override {
        // The caller may reuse the same view object while replacing its
        // mapped backing file. Rebuild the region on every call so a changed
        // byte length and active-voice mappings cannot remain stale.
        waveformMemory_ = view;
        externalMemoryBus_.clear();
        if (view != nullptr && view->isAttached() &&
            (view->sizeInBytes() & 1u) == 0u) {
            // Existing maps express absolute word addresses in this image.
            // Until a board-derived physical base is available, preserve that
            // contract as a PCM-ROM window beginning at address zero.
            const auto words = view->sizeInBytes() / 2u;
            if (words <= SAM9407ExternalMemoryBus::kAddressSpaceWordCount)
                (void) externalMemoryBus_.addReadOnlyRegion(
                    0u, static_cast<std::uint32_t>(words),
                    SAM9407MemoryDevice::pcmRom, *view);
        }
        // A newly attached/detached memory view changes whether an existing
        // bank/program can be resolved. Force one rebind on the next render.
        for (auto& state : voices_)
            state.mappingChecked = false;
    }

    void prepare(double sampleRate, int) override {
        hostSampleRate_ = sampleRate > 8000.0 ? sampleRate : 44100.0;
        for (auto& state : voices_)
            state = {};
        programRuntime_.reset();
    }

    void noteOn(int voiceIndex, const VoiceSlot& slot) noexcept override {
        if (!validVoice(voiceIndex))
            return;
        programRuntime_.noteOn(voiceIndex, slot);
        bindVoice(voiceIndex, slot);
    }

    void noteOff(int voiceIndex, const VoiceSlot&) noexcept override {
        if (validVoice(voiceIndex))
            voices_[static_cast<std::size_t>(voiceIndex)].released = true;
        programRuntime_.noteOff(voiceIndex);
    }

    void resetVoice(int voiceIndex, const VoiceSlot&) noexcept override {
        if (!validVoice(voiceIndex))
            return;
        voices_[static_cast<std::size_t>(voiceIndex)] = {};
        programRuntime_.resetVoice(voiceIndex);
    }

    bool isVoiceFinished(int voiceIndex) const noexcept override {
        return !validVoice(voiceIndex) ||
            !voices_[static_cast<std::size_t>(voiceIndex)].active;
    }

    void render(float* const* channels, int channelCount, int sampleCount,
                const VoiceManager& voiceManager, const ModulationMatrix&,
                const MASSPartParameterStore& parts,
                const MASSModulationSourceStore& sources,
                const MASSPartRuntimeControlStore& runtimeControls,
                const MASSCommonControlStore&,
                const MASSPerformanceImageStore&,
                const MASSDrumParameterStore&) noexcept override {
        if (channels == nullptr || channelCount <= 0 || sampleCount <= 0)
            return;

        const auto& slots = voiceManager.voices();
        for (std::size_t voiceIndex = 0; voiceIndex < slots.size(); ++voiceIndex) {
            const auto& slot = slots[voiceIndex];
            auto& state = voices_[voiceIndex];
            if (!slot.active) {
                state = {};
                programRuntime_.resetVoice(static_cast<int>(voiceIndex));
                continue;
            }

            // Keep the original IC51 program binding synchronized with the
            // authoritative VoiceSlot, including mono/legato retargeting.
            // Retargeting can change bank/program without a fresh noteOn call.
            programRuntime_.synchronizeVoice(static_cast<int>(voiceIndex), slot);

            if (!state.mappingChecked ||
                state.bank != slot.bankNumber ||
                state.program != slot.soundNumber ||
                state.note != slot.note ||
                state.velocity != slot.velocity)
                bindVoice(static_cast<int>(voiceIndex), slot);

            if (!state.active || waveformMemory_ == nullptr)
                continue;

            const int part = static_cast<int>(slot.part);
            const int rawLevel = parts.get(part, PartParameter::level);
            const float partGain = rawLevel < 0 ? 1.0f :
                static_cast<float>(rawLevel) / 127.0f;
            const float expression =
                static_cast<float>(sources.expression(part)) / 127.0f;

            const int bendRange = runtimeControls.rpn(part, 0) >= 0
                ? runtimeControls.rpn(part, 0) : 2;
            const int bendCents = runtimeControls.rpnLsb(part, 0) >= 0
                ? runtimeControls.rpnLsb(part, 0) : 0;
            const double bendSemitones =
                (static_cast<double>(sources.pitchBend(part)) / 8192.0) *
                (static_cast<double>(bendRange) +
                 static_cast<double>(bendCents) / 100.0);
            const double semitones =
                static_cast<double>(slot.note) -
                static_cast<double>(state.mapping.rootNote) +
                static_cast<double>(state.mapping.tuneCents) / 100.0 +
                bendSemitones;
            const double step =
                (static_cast<double>(state.mapping.sampleRate) / hostSampleRate_) *
                std::pow(2.0, semitones / 12.0);
            const float amplitude =
                (static_cast<float>(slot.velocity) / 127.0f) *
                (static_cast<float>(state.mapping.gain) / 127.0f) *
                partGain * expression;

            const int rawPan = parts.get(part, PartParameter::panorama);
            const float pan = rawPan >= 1 && rawPan <= 15
                ? static_cast<float>(rawPan - 1) / 14.0f
                : static_cast<float>(state.mapping.pan) / 127.0f;
            const float leftGain = 1.0f - pan;
            const float rightGain = pan;

            for (int sample = 0; sample < sampleCount && state.active; ++sample) {
                float value = 0.0f;
                if (!interpolatedSample(state, value)) {
                    state.active = false;
                    break;
                }
                const float output = value * amplitude;
                if (channelCount == 1) {
                    channels[0][sample] += output * 0.5f;
                } else {
                    channels[0][sample] += output * leftGain;
                    channels[1][sample] += output * rightGain;
                    for (int channel = 2; channel < channelCount; ++channel)
                        channels[channel][sample] += output * 0.5f;
                }
                advance(state, step);
            }
        }
    }

private:
    struct VoiceState {
        bool active = false;
        bool released = false;
        bool reverse = false;
        bool mappingChecked = false;
        std::uint8_t bank = 0;
        std::uint8_t program = 0;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        double position = 0.0;
        SAM9407SampleMapEntry mapping{};
    };

    std::array<SAM9407SampleMapEntry, kMaximumSampleMappings> sampleMap_{};
    std::size_t sampleMapCount_ = 0;
    std::array<VoiceState, VoiceManager::kVoiceCount> voices_{};
    MASSOriginalVoiceProgramRuntime programRuntime_{};
    const MASSWaveformMemoryView* waveformMemory_ = nullptr;
    SAM9407ExternalMemoryBus externalMemoryBus_{};
    double hostSampleRate_ = 44100.0;

    static bool validVoice(int index) noexcept {
        return index >= 0 &&
            index < static_cast<int>(VoiceManager::kVoiceCount);
    }

    const SAM9407SampleMapEntry* findMapping(
        std::uint8_t bank, std::uint8_t program,
        std::uint8_t note, std::uint8_t velocity) const noexcept {
        const SAM9407SampleMapEntry* best = nullptr;
        std::uint32_t bestArea = 0xffffffffu;
        std::uint16_t bestKeyWidth = 0xffffu;
        std::uint16_t bestVelocityWidth = 0xffffu;
        for (std::size_t i = 0; i < sampleMapCount_; ++i) {
            const auto& candidate = sampleMap_[i];
            if (candidate.bank != bank || candidate.program != program ||
                note < candidate.keyLow || note > candidate.keyHigh ||
                velocity < candidate.velocityLow || velocity > candidate.velocityHigh)
                continue;
            const auto keyWidth = static_cast<std::uint16_t>(
                candidate.keyHigh - candidate.keyLow + 1u);
            const auto velocityWidth = static_cast<std::uint16_t>(
                candidate.velocityHigh - candidate.velocityLow + 1u);
            const auto area = static_cast<std::uint32_t>(keyWidth) * velocityWidth;
            if (best == nullptr || area < bestArea ||
                (area == bestArea && keyWidth < bestKeyWidth) ||
                (area == bestArea && keyWidth == bestKeyWidth &&
                 velocityWidth < bestVelocityWidth)) {
                best = &candidate;
                bestArea = area;
                bestKeyWidth = keyWidth;
                bestVelocityWidth = velocityWidth;
            }
        }
        return best;
    }

    void bindVoice(int voiceIndex, const VoiceSlot& slot) noexcept {
        auto& state = voices_[static_cast<std::size_t>(voiceIndex)];
        state = {};
        state.mappingChecked = true;
        state.bank = slot.bankNumber;
        state.program = slot.soundNumber;
        state.note = slot.note;
        state.velocity = slot.velocity;
        const auto* mapping = findMapping(slot.bankNumber, slot.soundNumber,
                                           slot.note, slot.velocity);
        if (mapping == nullptr ||
            !externalMemoryBus_.containsReadableRange(
                mapping->startWord, mapping->endWord))
            return;

        state.mapping = *mapping;
        state.position = static_cast<double>(mapping->startWord);
        state.active = true;
    }

    bool readSampleWord(
        std::uint32_t wordAddress, const SAM9407SampleMapEntry& mapping,
        float& value) const noexcept {
        std::uint16_t bits = 0;
        if (!externalMemoryBus_.readWord(
                wordAddress, SAM9407MemoryCycle::synthesisAccess, bits))
            return false;
        if (mapping.byteOrder == SAM9407SampleByteOrder::bigEndian) {
            bits = static_cast<std::uint16_t>(
                (bits << 8) | (bits >> 8));
        }
        const auto signedValue = static_cast<std::int16_t>(bits);
        value = static_cast<float>(signedValue) / 32768.0f;
        return true;
    }

    bool interpolatedSample(const VoiceState& state, float& result) const noexcept {
        const auto& mapping = state.mapping;
        if (state.position < static_cast<double>(mapping.startWord) ||
            state.position >= static_cast<double>(mapping.endWord))
            return false;

        // The sample coordinate is independent of playback direction:
        // floor(position) and the next word bracket the same fractional
        // location whether the phase is moving forward or backward. The
        // reverse flag changes phase advancement only; choosing the previous
        // word here would interpolate from the wrong side on alternating loops.
        const auto firstWord = static_cast<std::uint32_t>(state.position);
        auto secondWord = firstWord + 1u;
        if (secondWord >= mapping.endWord)
            secondWord = mapping.endWord - 1u;
        if (!state.released &&
            mapping.loopMode != SAM9407LoopMode::oneShot &&
            secondWord >= mapping.loopEndWord)
            secondWord = mapping.loopStartWord;

        float first = 0.0f, second = 0.0f;
        if (!readSampleWord(firstWord, mapping, first) ||
            !readSampleWord(secondWord, mapping, second))
            return false;
        const float fraction =
            static_cast<float>(state.position - static_cast<double>(firstWord));
        result = first + (second - first) * fraction;
        return true;
    }

    static void advance(VoiceState& state, double step) noexcept {
        const auto& mapping = state.mapping;

        if (!state.released &&
            mapping.loopMode == SAM9407LoopMode::alternating) {
            const double loopStart = static_cast<double>(mapping.loopStartWord);
            const double loopEnd = static_cast<double>(mapping.loopEndWord);
            // The upper turn point is half a sample before the exclusive
            // loop end. This preserves the renderer's interpolated end-to-start
            // seam while folding arbitrarily large pitch steps into the loop.
            const double loopSpan = loopEnd - loopStart - 0.5;
            if (loopSpan > 0.0) {
                double phase = 0.0;
                if (!state.reverse) {
                    const double nextPosition = state.position + step;
                    // Keep the one-shot attack segment before loopStart intact.
                    if (nextPosition <= loopStart) {
                        state.position = nextPosition;
                        return;
                    }
                    phase = nextPosition - loopStart;
                } else {
                    phase = 2.0 * loopSpan -
                        (state.position - loopStart) + step;
                }

                const double period = 2.0 * loopSpan;
                phase = std::fmod(phase, period);
                if (phase < 0.0)
                    phase += period;

                if (phase <= loopSpan) {
                    state.position = loopStart + phase;
                    state.reverse = false;
                } else {
                    state.position = loopStart + (period - phase);
                    state.reverse = true;
                }
                return;
            }
        }

        state.position += state.reverse ? -step : step;

        if (state.released || mapping.loopMode == SAM9407LoopMode::oneShot) {
            if (state.position >= static_cast<double>(mapping.endWord) ||
                state.position < static_cast<double>(mapping.startWord))
                state.active = false;
            return;
        }

        const double loopStart = static_cast<double>(mapping.loopStartWord);
        const double loopEnd = static_cast<double>(mapping.loopEndWord);
        const double loopLength = loopEnd - loopStart;
        if (loopLength <= 0.0)
            return;

        if (mapping.loopMode == SAM9407LoopMode::forward) {
            if (!state.reverse && state.position >= loopEnd)
                state.position = loopStart + std::fmod(state.position - loopStart, loopLength);
        }
    }
};

} // namespace technox
