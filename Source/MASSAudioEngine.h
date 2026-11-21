#pragma once
#include "MASSModulationMatrix.h"
#include "MASSVoiceManager.h"
#include <cstddef>
#include <cstdint>

namespace technox {

// Backend contract only. A real implementation must be based on verified
// TechnoX sample/algorithm evidence; the included null backend is intentionally silent.
class IMASSVoiceBackend {
public:
    virtual ~IMASSVoiceBackend() = default;
    virtual void prepare(double sampleRate, int maximumBlockSize) = 0;
    virtual void noteOn(int voiceIndex, const VoiceSlot& voice) noexcept = 0;
    virtual void noteOff(int voiceIndex, const VoiceSlot& voice) noexcept = 0;
    virtual void render(float* const* channels, int channelCount, int sampleCount,
                        const VoiceManager& voices) noexcept = 0;
};

class NullMASSVoiceBackend final : public IMASSVoiceBackend {
public:
    void prepare(double, int) override {}
    void noteOn(int, const VoiceSlot&) noexcept override {}
    void noteOff(int, const VoiceSlot&) noexcept override {}
    void render(float* const* channels, int channelCount, int sampleCount,
                const VoiceManager&) noexcept override {
        for (int channel = 0; channel < channelCount; ++channel)
            for (int sample = 0; sample < sampleCount; ++sample)
                channels[channel][sample] = 0.0f;
    }
};

class MASSAudioEngine {
public:
    explicit MASSAudioEngine(IMASSVoiceBackend* backend = nullptr) noexcept
        : backend_(backend != nullptr ? backend : &nullBackend_) {}

    void prepare(double sampleRate, int maximumBlockSize) {
        backend_->prepare(sampleRate, maximumBlockSize);
    }

    void noteOn(std::uint8_t part, std::uint8_t channel,
                std::uint8_t note, std::uint8_t velocity) noexcept {
        const int voiceIndex = voices_.noteOn(part, channel, note, velocity);
        if (voiceIndex >= 0)
            backend_->noteOn(voiceIndex, voices_.voices()[static_cast<std::size_t>(voiceIndex)]);
    }

    void noteOff(std::uint8_t channel, std::uint8_t note) noexcept {
        voices_.noteOff(channel, note);
        const auto& slots = voices_.voices();
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].active && slots[i].releasing && slots[i].channel == channel && slots[i].note == note)
                backend_->noteOff(static_cast<int>(i), slots[i]);
    }

    void allNotesOff(int channel = 0) noexcept {
        voices_.allNotesOff(channel);
        const auto& slots = voices_.voices();
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].active && slots[i].releasing && (channel == 0 || slots[i].channel == channel))
                backend_->noteOff(static_cast<int>(i), slots[i]);
    }

    void render(float* const* channels, int channelCount, int sampleCount) noexcept {
        backend_->render(channels, channelCount, sampleCount, voices_);
    }

    VoiceManager& voiceManager() noexcept { return voices_; }
    const VoiceManager& voiceManager() const noexcept { return voices_; }
    ModulationMatrix& modulationMatrix() noexcept { return modulation_; }
    const ModulationMatrix& modulationMatrix() const noexcept { return modulation_; }

private:
    NullMASSVoiceBackend nullBackend_;
    IMASSVoiceBackend* backend_;
    VoiceManager voices_;
    ModulationMatrix modulation_;
};

} // namespace technox
