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

enum class MASSCommonControl : std::size_t { performanceMode = 0, fx1Type, fx2Type, arpResolution, arpSpeed };

class MASSCommonControlStore {
public:
    static constexpr std::size_t kControlCount = 5;
    MASSCommonControlStore() {
        for (auto& value : values_) value.store(-1, std::memory_order_relaxed);
    }
    bool set(MASSCommonControl control, int value) noexcept {
        const auto index = static_cast<std::size_t>(control);
        constexpr int maxima[kControlCount] = {3, 21, 21, 3, 127};
        if (index >= kControlCount || value < 0 || value > maxima[index]) return false;
        values_[index].store(value, std::memory_order_release);
        return true;
    }
    int get(MASSCommonControl control) const noexcept {
        const auto index = static_cast<std::size_t>(control);
        return index < kControlCount ? values_[index].load(std::memory_order_acquire) : -1;
    }
    void clear(MASSCommonControl control) noexcept {
        const auto index = static_cast<std::size_t>(control);
        if (index < kControlCount) values_[index].store(-1, std::memory_order_release);
    }
private:
    std::array<std::atomic<int>, kControlCount> values_{};
};

class MASSDrumParameterStore {
public:
    static constexpr std::size_t kPartCount = 16;
    static constexpr std::size_t kKeyCount = 128;
    static constexpr std::size_t kParameterCount = 5;
    MASSDrumParameterStore() {
        for (auto& part : values_)
            for (auto& key : part)
                for (auto& value : key)
                    value.store(-1, std::memory_order_relaxed);
    }
    bool set(int partIndex, int keyNumber, int parameterIndex, int value) noexcept {
        constexpr int maxima[kParameterCount] = {48, 127, 19, 63, 63};
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            keyNumber < 0 || keyNumber >= static_cast<int>(kKeyCount) ||
            parameterIndex < 0 || parameterIndex >= static_cast<int>(kParameterCount) ||
            value < 0 || value > maxima[parameterIndex])
            return false;
        values_[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(keyNumber)]
               [static_cast<std::size_t>(parameterIndex)].store(value, std::memory_order_release);
        return true;
    }
    int get(int partIndex, int keyNumber, int parameterIndex) const noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            keyNumber < 0 || keyNumber >= static_cast<int>(kKeyCount) ||
            parameterIndex < 0 || parameterIndex >= static_cast<int>(kParameterCount))
            return -1;
        return values_[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(keyNumber)]
                      [static_cast<std::size_t>(parameterIndex)].load(std::memory_order_acquire);
    }
private:
    std::array<std::array<std::array<std::atomic<int>, kParameterCount>, kKeyCount>, kPartCount> values_{};
};

class MASSModulationSourceStore {
public:
    static constexpr std::size_t kPartCount = 16;
    MASSModulationSourceStore() {
        for (std::size_t i = 0; i < kPartCount; ++i) {
            modWheel_[i].store(0, std::memory_order_relaxed);
            pitchBend_[i].store(0, std::memory_order_relaxed);
            expression_[i].store(127, std::memory_order_relaxed);
        }
    }
    bool setModWheel(int partIndex, int value) noexcept {
        if (!valid(partIndex) || value < 0 || value > 127) return false;
        modWheel_[static_cast<std::size_t>(partIndex)].store(value, std::memory_order_release);
        return true;
    }
    int modWheel(int partIndex) const noexcept {
        return valid(partIndex) ? modWheel_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire) : 0;
    }
    bool setPitchBend(int partIndex, int value) noexcept {
        if (!valid(partIndex) || value < -8192 || value > 8191) return false;
        pitchBend_[static_cast<std::size_t>(partIndex)].store(value, std::memory_order_release);
        return true;
    }
    int pitchBend(int partIndex) const noexcept {
        return valid(partIndex) ? pitchBend_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire) : 0;
    }
    bool setExpression(int partIndex, int value) noexcept {
        if (!valid(partIndex) || value < 0 || value > 127) return false;
        expression_[static_cast<std::size_t>(partIndex)].store(value, std::memory_order_release);
        return true;
    }
    int expression(int partIndex) const noexcept {
        return valid(partIndex) ? expression_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire) : 127;
    }
    void reset(int partIndex) noexcept {
        if (!valid(partIndex)) return;
        const auto i = static_cast<std::size_t>(partIndex);
        modWheel_[i].store(0, std::memory_order_release);
        pitchBend_[i].store(0, std::memory_order_release);
        expression_[i].store(127, std::memory_order_release);
    }
private:
    static bool valid(int partIndex) noexcept { return partIndex >= 0 && partIndex < static_cast<int>(kPartCount); }
    std::array<std::atomic<int>, kPartCount> modWheel_{};
    std::array<std::atomic<int>, kPartCount> pitchBend_{};
    std::array<std::atomic<int>, kPartCount> expression_{};
};

class MASSPartRuntimeControlStore {
public:
    static constexpr std::size_t kPartCount = 16;
    MASSPartRuntimeControlStore() {
        for (std::size_t i = 0; i < kPartCount; ++i) {
            portamento_[i].store(0, std::memory_order_relaxed);
            softPedal_[i].store(0, std::memory_order_relaxed);
            for (auto& rpn : rpnValues_[i]) rpn.store(-1, std::memory_order_relaxed);
        }
    }
    bool setPortamento(int partIndex, bool enabled) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount)) return false;
        portamento_[static_cast<std::size_t>(partIndex)].store(enabled ? 1 : 0, std::memory_order_release);
        return true;
    }
    bool setSoftPedal(int partIndex, bool enabled) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount)) return false;
        softPedal_[static_cast<std::size_t>(partIndex)].store(enabled ? 1 : 0, std::memory_order_release);
        return true;
    }
    bool portamento(int partIndex) const noexcept {
        return partIndex >= 0 && partIndex < static_cast<int>(kPartCount) &&
            portamento_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire) != 0;
    }
    bool softPedal(int partIndex) const noexcept {
        return partIndex >= 0 && partIndex < static_cast<int>(kPartCount) &&
            softPedal_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire) != 0;
    }
    bool setRpn(int partIndex, int parameterIndex, int value) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            parameterIndex < 0 || parameterIndex >= 3 || value < 0 || value > 127)
            return false;
        rpnValues_[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(parameterIndex)]
            .store(value, std::memory_order_release);
        return true;
    }
    int rpn(int partIndex, int parameterIndex) const noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            parameterIndex < 0 || parameterIndex >= 3) return -1;
        return rpnValues_[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(parameterIndex)]
            .load(std::memory_order_acquire);
    }
    void reset(int partIndex) noexcept {
        setPortamento(partIndex, false);
        setSoftPedal(partIndex, false);
        if (partIndex >= 0 && partIndex < static_cast<int>(kPartCount))
            for (auto& rpn : rpnValues_[static_cast<std::size_t>(partIndex)])
                rpn.store(-1, std::memory_order_release);
    }
private:
    std::array<std::atomic<int>, kPartCount> portamento_{};
    std::array<std::atomic<int>, kPartCount> softPedal_{};
    std::array<std::array<std::atomic<int>, 3>, kPartCount> rpnValues_{};
};

// Backend contract only. A real implementation must be based on verified
// TechnoX sample/algorithm evidence; the included null backend is intentionally silent.
class IMASSVoiceBackend {
public:
    virtual ~IMASSVoiceBackend() = default;
    virtual void prepare(double sampleRate, int maximumBlockSize) = 0;
    virtual void noteOn(int voiceIndex, const VoiceSlot& voice) noexcept = 0;
    virtual void noteOff(int voiceIndex, const VoiceSlot& voice) noexcept = 0;
    virtual void resetVoice(int voiceIndex, const VoiceSlot& voice) noexcept = 0;
    virtual bool isVoiceFinished(int voiceIndex) const noexcept = 0;
    virtual void render(float* const* channels, int channelCount, int sampleCount,
                        const VoiceManager& voices, const ModulationMatrix& matrix,
                        const MASSPartParameterStore& parts,
                        const MASSModulationSourceStore& sources,
                        const MASSPartRuntimeControlStore& runtimeControls,
                        const MASSCommonControlStore& common,
                        const MASSDrumParameterStore& drums) noexcept = 0;
};

class NullMASSVoiceBackend final : public IMASSVoiceBackend {
public:
    void prepare(double, int) override {}
    void noteOn(int, const VoiceSlot&) noexcept override {}
    void noteOff(int, const VoiceSlot&) noexcept override {}
    void resetVoice(int, const VoiceSlot&) noexcept override {}
    bool isVoiceFinished(int) const noexcept override { return true; }
    void render(float* const* channels, int channelCount, int sampleCount,
                const VoiceManager&, const ModulationMatrix&, const MASSPartParameterStore&,
                const MASSModulationSourceStore&, const MASSPartRuntimeControlStore&,
                const MASSCommonControlStore&, const MASSDrumParameterStore&) noexcept override {
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
        voices_.reset();
    }

    void noteOn(std::uint8_t part, std::uint8_t channel,
                std::uint8_t note, std::uint8_t velocity) noexcept {
        if (velocity > 0) {
            const int target = voices_.allocationTarget();
            if (target >= 0 && voices_.voices()[static_cast<std::size_t>(target)].active)
                backend_->noteOff(target, voices_.voices()[static_cast<std::size_t>(target)]);
        }
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

    void allSoundsOff(int channel = 0) noexcept {
        const auto& slots = voices_.voices();
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].active && (channel == 0 || slots[i].channel == channel)) {
                backend_->resetVoice(static_cast<int>(i), slots[i]);
                voices_.finishRelease(i);
            }
        }
    }

    bool setDrumParameter(int partIndex, int keyNumber, int parameterIndex, int value) noexcept { return drumParameters_.set(partIndex, keyNumber, parameterIndex, value); }
    int getDrumParameter(int partIndex, int keyNumber, int parameterIndex) const noexcept { return drumParameters_.get(partIndex, keyNumber, parameterIndex); }

    bool setCommonControl(MASSCommonControl control, int value) noexcept { return commonControls_.set(control, value); }
    int getCommonControl(MASSCommonControl control) const noexcept { return commonControls_.get(control); }
    void clearCommonControl(MASSCommonControl control) noexcept { commonControls_.clear(control); }

    bool setPartParameter(int partIndex, PartParameter parameter, int rawValue) noexcept {
        return partParameters_.set(partIndex, parameter, rawValue);
    }
    int getPartParameter(int partIndex, PartParameter parameter) const noexcept {
        return partParameters_.get(partIndex, parameter);
    }
    bool setModWheel(int partIndex, int value) noexcept { return modulationSources_.setModWheel(partIndex, value); }
    int getModWheel(int partIndex) const noexcept { return modulationSources_.modWheel(partIndex); }
    bool setPitchBend(int partIndex, int value) noexcept { return modulationSources_.setPitchBend(partIndex, value); }
    int getPitchBend(int partIndex) const noexcept { return modulationSources_.pitchBend(partIndex); }
    bool setExpression(int partIndex, int value) noexcept { return modulationSources_.setExpression(partIndex, value); }
    int getExpression(int partIndex) const noexcept { return modulationSources_.expression(partIndex); }
    bool setPortamento(int partIndex, bool enabled) noexcept { return runtimeControls_.setPortamento(partIndex, enabled); }
    bool setSoftPedal(int partIndex, bool enabled) noexcept { return runtimeControls_.setSoftPedal(partIndex, enabled); }
    bool portamento(int partIndex) const noexcept { return runtimeControls_.portamento(partIndex); }
    bool softPedal(int partIndex) const noexcept { return runtimeControls_.softPedal(partIndex); }
    bool setRpn(int partIndex, int parameterIndex, int value) noexcept { return runtimeControls_.setRpn(partIndex, parameterIndex, value); }
    int getRpn(int partIndex, int parameterIndex) const noexcept { return runtimeControls_.rpn(partIndex, parameterIndex); }
    void resetPartControllers(int partIndex) noexcept { runtimeControls_.reset(partIndex); modulationSources_.reset(partIndex); }

    void render(float* const* channels, int channelCount, int sampleCount) noexcept {
        backend_->render(channels, channelCount, sampleCount, voices_, modulation_, partParameters_, modulationSources_, runtimeControls_, commonControls_, drumParameters_);
        for (std::size_t i = 0; i < voices_.voices().size(); ++i) {
            const auto& voice = voices_.voices()[i];
            if (voice.active && voice.releasing && backend_->isVoiceFinished(static_cast<int>(i)))
                voices_.finishRelease(i);
        }
    }

    void finishVoiceRelease(std::size_t voiceIndex) noexcept { voices_.finishRelease(voiceIndex); }
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
    MASSPartParameterStore partParameters_;
    MASSModulationSourceStore modulationSources_;
    MASSPartRuntimeControlStore runtimeControls_;
    MASSCommonControlStore commonControls_;
    MASSDrumParameterStore drumParameters_;
};

} // namespace technox
