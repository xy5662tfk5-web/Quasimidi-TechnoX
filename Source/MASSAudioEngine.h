#pragma once
#include "MASSModulationMatrix.h"
#include "MASSVoiceManager.h"
#include "TechnoXPartModel.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace technox {

class MASSPartParameterStore {
public:
    static constexpr std::size_t kPartCount = 16;
    static constexpr std::size_t kParameterCount = 24;

    MASSPartParameterStore() {
        for (auto& part : values_)
            for (auto& value : part)
                value.store(0, std::memory_order_relaxed);
    }

    bool set(int partIndex, PartParameter parameter, int rawValue) noexcept {
        const auto parameterIndex = static_cast<std::size_t>(parameter);
        const auto* descriptor = PartEditModel::descriptor(parameterIndex);
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            descriptor == nullptr || rawValue < 0 || rawValue > descriptor->maximum)
            return false;
        values_[static_cast<std::size_t>(partIndex)][parameterIndex]
            .store(rawValue, std::memory_order_release);
        return true;
    }

    int get(int partIndex, PartParameter parameter) const noexcept {
        const auto parameterIndex = static_cast<std::size_t>(parameter);
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            parameterIndex >= kParameterCount)
            return -1;
        return values_[static_cast<std::size_t>(partIndex)][parameterIndex]
            .load(std::memory_order_acquire);
    }

private:
    std::array<std::array<std::atomic<int>, kParameterCount>, kPartCount> values_{};
};

class MASSModulationSourceStore {
public:
    static constexpr std::size_t kPartCount = 16;
    MASSModulationSourceStore() {
        for (auto& value : modWheel_)
            value.store(0, std::memory_order_relaxed);
    }
    bool setModWheel(int partIndex, int value) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) || value < 0 || value > 127)
            return false;
        modWheel_[static_cast<std::size_t>(partIndex)].store(value, std::memory_order_release);
        return true;
    }
    int modWheel(int partIndex) const noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount)) return 0;
        return modWheel_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire);
    }
private:
    std::array<std::atomic<int>, kPartCount> modWheel_{};
};

// Backend contract only. A real implementation must be based on verified
// TechnoX sample/algorithm evidence; the included null backend is intentionally silent.
class IMASSVoiceBackend {
public:
    virtual ~IMASSVoiceBackend() = default;
    virtual void prepare(double sampleRate, int maximumBlockSize) = 0;
    virtual void noteOn(int voiceIndex, const VoiceSlot& voice) noexcept = 0;
    virtual void noteOff(int voiceIndex, const VoiceSlot& voice) noexcept = 0;
    virtual void render(float* const* channels, int channelCount, int sampleCount,
                        const VoiceManager& voices, const MASSPartParameterStore& parts,
                        const MASSModulationSourceStore& sources) noexcept = 0;
};

class NullMASSVoiceBackend final : public IMASSVoiceBackend {
public:
    void prepare(double, int) override {}
    void noteOn(int, const VoiceSlot&) noexcept override {}
    void noteOff(int, const VoiceSlot&) noexcept override {}
    void render(float* const* channels, int channelCount, int sampleCount,
                const VoiceManager&, const MASSPartParameterStore&,
                const MASSModulationSourceStore&) noexcept override {
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

    bool setPartParameter(int partIndex, PartParameter parameter, int rawValue) noexcept {
        return partParameters_.set(partIndex, parameter, rawValue);
    }
    int getPartParameter(int partIndex, PartParameter parameter) const noexcept {
        return partParameters_.get(partIndex, parameter);
    }

    void render(float* const* channels, int channelCount, int sampleCount) noexcept {
        backend_->render(channels, channelCount, sampleCount, voices_, partParameters_, modulationSources_);
    }

    VoiceManager& voiceManager() noexcept { return voices_; }
    const VoiceManager& voiceManager() const noexcept { return voices_; }
    ModulationMatrix& modulationMatrix() noexcept { return modulation_; }
    const ModulationMatrix& modulationMatrix() const noexcept { return modulation_; }
    const MASSPartParameterStore& partParameters() const noexcept { return partParameters_; }

private:
    NullMASSVoiceBackend nullBackend_;
    IMASSVoiceBackend* backend_;
    VoiceManager voices_;
    ModulationMatrix modulation_;
    MASSPartParameterStore partParameters_;\n    MASSModulationSourceStore modulationSources_;
};

} // namespace technox
