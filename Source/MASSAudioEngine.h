#pragma once
#include <algorithm>
#include "MASSModulationMatrix.h"
#include "MASSVoiceManager.h"
#include "TechnoXPartModel.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace technox {

class MASSResourceProgramCatalog;
class MASSFXResourceCatalog;

class MASSPartParameterStore {
public:
    static constexpr std::size_t kPartCount = 16;
    static constexpr std::size_t kParameterCount = 24;

    MASSPartParameterStore() {
        for (auto& part : values_)
            for (auto& value : part)
                value.store(0, std::memory_order_relaxed);
        for (auto& part : values_)
            part[static_cast<std::size_t>(PartParameter::trackMode)].store(1, std::memory_order_relaxed);
        // Neutral defaults for parameters with signed display offsets.
        for (auto& part : values_) {
            part[static_cast<std::size_t>(PartParameter::transpose)].store(24, std::memory_order_relaxed);
            part[static_cast<std::size_t>(PartParameter::tune)].store(64, std::memory_order_relaxed);
        }
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
    static constexpr int kFXTypeMaximumIndex = 20; // IC4 0x7FF2 clamps values >= 21 to 0x14.
    static constexpr std::size_t kModulationSourceCount = 4;
    static constexpr std::size_t kModulationDestinationCount = 8;
    MASSCommonControlStore() {
        for (auto& value : values_) value.store(-1, std::memory_order_relaxed);
        for (auto& source : modulationDepths_)
            for (auto& depth : source) depth.store(-1, std::memory_order_relaxed);
    }
    bool set(MASSCommonControl control, int value) noexcept {
        const auto index = static_cast<std::size_t>(control);
        constexpr int maxima[kControlCount] = {3, kFXTypeMaximumIndex,
            kFXTypeMaximumIndex, 3, 127};
        if (index >= kControlCount || value < 0) return false;
        if (index == static_cast<std::size_t>(MASSCommonControl::fx1Type) ||
            index == static_cast<std::size_t>(MASSCommonControl::fx2Type))
            value = std::min(value, kFXTypeMaximumIndex);
        else if (value > maxima[index])
            return false;
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
    bool setModulationDepth(int sourceIndex, int destinationIndex, int rawValue) noexcept {
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(kModulationSourceCount) ||
            destinationIndex < 0 || destinationIndex >= static_cast<int>(kModulationDestinationCount) ||
            rawValue < 0 || rawValue > 127)
            return false;
        modulationDepths_[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(destinationIndex)]
            .store(rawValue, std::memory_order_release);
        return true;
    }
    int modulationDepth(int sourceIndex, int destinationIndex) const noexcept {
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(kModulationSourceCount) ||
            destinationIndex < 0 || destinationIndex >= static_cast<int>(kModulationDestinationCount))
            return -1;
        return modulationDepths_[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(destinationIndex)]
            .load(std::memory_order_acquire);
    }
    void clearModulationDepths() noexcept {
        for (auto& source : modulationDepths_)
            for (auto& depth : source) depth.store(-1, std::memory_order_release);
    }
private:
    std::array<std::atomic<int>, kControlCount> values_{};
    std::array<std::array<std::atomic<int>, kModulationDestinationCount>, kModulationSourceCount>
        modulationDepths_{};
};

// Lossless 169-byte Performance image recovered from IC4 routine 0xB475:
// 8-byte name, 65 Common bytes, then four 24-byte Part records.
class MASSPerformanceImageStore {
public:
    static constexpr std::size_t kNameByteCount = 8;
    static constexpr std::size_t kCommonByteCount = 65;
    static constexpr std::size_t kPartCount = 4;
    static constexpr std::size_t kPartByteCount = 24;
    static constexpr std::size_t kSerializedSize =
        kNameByteCount + kCommonByteCount + kPartCount * kPartByteCount;
    MASSPerformanceImageStore() { clear(); }
    void clear() noexcept {
        for (auto& value : nameBytes_) value.store(0, std::memory_order_relaxed);
        for (auto& value : commonBytes_) value.store(0, std::memory_order_relaxed);
        for (auto& part : partBytes_)
            for (auto& value : part) value.store(0, std::memory_order_relaxed);
        loaded_.store(false, std::memory_order_release);
    }
    bool setNameByte(int index, int value) noexcept {
        if (index < 0 || index >= static_cast<int>(kNameByteCount) || value < 0 || value > 127) return false;
        nameBytes_[static_cast<std::size_t>(index)].store(value, std::memory_order_release);
        return true;
    }
    int nameByte(int index) const noexcept {
        if (index < 0 || index >= static_cast<int>(kNameByteCount)) return -1;
        return nameBytes_[static_cast<std::size_t>(index)].load(std::memory_order_acquire);
    }
    bool setCommonByte(int index, int value) noexcept {
        if (index < 0 || index >= static_cast<int>(kCommonByteCount) || value < 0 || value > 127) return false;
        commonBytes_[static_cast<std::size_t>(index)].store(value, std::memory_order_release);
        return true;
    }
    int commonByte(int index) const noexcept {
        if (index < 0 || index >= static_cast<int>(kCommonByteCount)) return -1;
        return commonBytes_[static_cast<std::size_t>(index)].load(std::memory_order_acquire);
    }
    bool setPartByte(int partIndex, int byteIndex, int value) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            byteIndex < 0 || byteIndex >= static_cast<int>(kPartByteCount) || value < 0 || value > 127) return false;
        partBytes_[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(byteIndex)]
            .store(value, std::memory_order_release);
        return true;
    }
    int partByte(int partIndex, int byteIndex) const noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            byteIndex < 0 || byteIndex >= static_cast<int>(kPartByteCount)) return -1;
        return partBytes_[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(byteIndex)]
            .load(std::memory_order_acquire);
    }
    bool copyTo(std::array<std::uint8_t, kSerializedSize>& destination) const noexcept {
        if (!isLoaded()) return false;
        std::size_t offset = 0;
        for (std::size_t i = 0; i < kNameByteCount; ++i)
            destination[offset++] = static_cast<std::uint8_t>(nameByte(static_cast<int>(i)));
        for (std::size_t i = 0; i < kCommonByteCount; ++i)
            destination[offset++] = static_cast<std::uint8_t>(commonByte(static_cast<int>(i)));
        for (std::size_t part = 0; part < kPartCount; ++part)
            for (std::size_t byte = 0; byte < kPartByteCount; ++byte)
                destination[offset++] = static_cast<std::uint8_t>(
                    partByte(static_cast<int>(part), static_cast<int>(byte)));
        return true;
    }
    void setLoaded(bool loaded) noexcept { loaded_.store(loaded, std::memory_order_release); }
    bool isLoaded() const noexcept { return loaded_.load(std::memory_order_acquire); }
private:
    std::array<std::atomic<int>, kNameByteCount> nameBytes_{};
    std::array<std::atomic<int>, kCommonByteCount> commonBytes_{};
    std::array<std::array<std::atomic<int>, kPartByteCount>, kPartCount> partBytes_{};
    std::atomic<bool> loaded_{false};
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
            aftertouch_[i].store(0, std::memory_order_relaxed);
            freeController_[i].store(0, std::memory_order_relaxed);
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
    bool setAftertouch(int partIndex, int value) noexcept {
        if (!valid(partIndex) || value < 0 || value > 127) return false;
        aftertouch_[static_cast<std::size_t>(partIndex)].store(value, std::memory_order_release);
        return true;
    }
    int aftertouch(int partIndex) const noexcept {
        return valid(partIndex) ? aftertouch_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire) : 0;
    }
    bool setFreeController(int partIndex, int value) noexcept {
        if (!valid(partIndex) || value < 0 || value > 127) return false;
        freeController_[static_cast<std::size_t>(partIndex)].store(value, std::memory_order_release);
        return true;
    }
    int freeController(int partIndex) const noexcept {
        return valid(partIndex) ? freeController_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire) : 0;
    }
    void reset(int partIndex) noexcept {
        if (!valid(partIndex)) return;
        const auto i = static_cast<std::size_t>(partIndex);
        modWheel_[i].store(0, std::memory_order_release);
        pitchBend_[i].store(0, std::memory_order_release);
        expression_[i].store(127, std::memory_order_release);
        aftertouch_[i].store(0, std::memory_order_release);
        freeController_[i].store(0, std::memory_order_release);
    }
private:
    static bool valid(int partIndex) noexcept { return partIndex >= 0 && partIndex < static_cast<int>(kPartCount); }
    std::array<std::atomic<int>, kPartCount> modWheel_{};
    std::array<std::atomic<int>, kPartCount> pitchBend_{};
    std::array<std::atomic<int>, kPartCount> expression_{};
    std::array<std::atomic<int>, kPartCount> aftertouch_{};
    std::array<std::atomic<int>, kPartCount> freeController_{};
};

class MASSPartRuntimeControlStore {
public:
    static constexpr std::size_t kPartCount = 16;
    MASSPartRuntimeControlStore() {
        for (std::size_t i = 0; i < kPartCount; ++i) {
            portamento_[i].store(0, std::memory_order_relaxed);
            softPedal_[i].store(0, std::memory_order_relaxed);
            for (auto& rpn : rpnValues_[i]) rpn.store(-1, std::memory_order_relaxed);
            for (auto& rpn : rpnLsbValues_[i]) rpn.store(-1, std::memory_order_relaxed);
            sustainPedal_[i].store(false, std::memory_order_relaxed);
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
    bool setSustainPedal(int partIndex, bool enabled) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount)) return false;
        sustainPedal_[static_cast<std::size_t>(partIndex)].store(enabled ? 1 : 0, std::memory_order_release);
        return true;
    }
    bool sustainPedal(int partIndex) const noexcept {
        return partIndex >= 0 && partIndex < static_cast<int>(kPartCount) &&
            sustainPedal_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire) != 0;
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
    bool setRpnLsb(int partIndex, int parameterIndex, int value) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            parameterIndex < 0 || parameterIndex >= 3 || value < 0 || value > 127)
            return false;
        rpnLsbValues_[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(parameterIndex)]
            .store(value, std::memory_order_release);
        return true;
    }
    int rpnLsb(int partIndex, int parameterIndex) const noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            parameterIndex < 0 || parameterIndex >= 3) return -1;
        return rpnLsbValues_[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(parameterIndex)]
            .load(std::memory_order_acquire);
    }
    void reset(int partIndex) noexcept {
        setPortamento(partIndex, false);
        setSoftPedal(partIndex, false);
        setSustainPedal(partIndex, false);
        if (partIndex >= 0 && partIndex < static_cast<int>(kPartCount))
            for (auto& rpn : rpnValues_[static_cast<std::size_t>(partIndex)])
                rpn.store(-1, std::memory_order_release);
        if (partIndex >= 0 && partIndex < static_cast<int>(kPartCount))
            for (auto& rpn : rpnLsbValues_[static_cast<std::size_t>(partIndex)])
                rpn.store(-1, std::memory_order_release);
    }
private:
    std::array<std::atomic<int>, kPartCount> portamento_{};
    std::array<std::atomic<int>, kPartCount> softPedal_{};
    std::array<std::atomic<int>, kPartCount> sustainPedal_{};
    std::array<std::array<std::atomic<int>, 3>, kPartCount> rpnValues_{};
    std::array<std::array<std::atomic<int>, 3>, kPartCount> rpnLsbValues_{};
};

// Backend contract only. A real implementation must be based on verified
// TechnoX sample/algorithm evidence; the included null backend is intentionally silent.
class IMASSVoiceBackend {
public:
    virtual ~IMASSVoiceBackend() = default;
    virtual void setProgramCatalog(const MASSResourceProgramCatalog*) noexcept {}
    virtual void setFXResourceCatalog(const MASSFXResourceCatalog*) noexcept {}
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
                        const MASSPerformanceImageStore& performanceImage,
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
                const MASSCommonControlStore&, const MASSPerformanceImageStore&,
                const MASSDrumParameterStore&) noexcept override {
        for (int channel = 0; channel < channelCount; ++channel)
            for (int sample = 0; sample < sampleCount; ++sample)
                channels[channel][sample] = 0.0f;
    }
};

class MASSAudioEngine {
public:
    explicit MASSAudioEngine(IMASSVoiceBackend* backend = nullptr) noexcept
        : backend_(backend != nullptr ? backend : &nullBackend_) {
        for (auto& pedalDown : sustainControllerDown_)
            pedalDown.store(false, std::memory_order_relaxed);
    }

    // Install a different DSP implementation while the host is stopped.
    // The engine owns neither backend; the supplied object must outlive it.
    // This is the hand-off point for the recovered original TechnoX M.A.S.S.
    // backend. Existing voices are discarded so no state leaks between DSPs.
    void setBackend(IMASSVoiceBackend* backend) noexcept {
        for (std::size_t i = 0; i < voices_.voices().size(); ++i)
            if (voices_.voices()[i].active)
                backend_->resetVoice(static_cast<int>(i), voices_.voices()[i]);
        voices_.reset();
        backend_ = backend != nullptr ? backend : &nullBackend_;
        backend_->setProgramCatalog(programCatalog_);
        backend_->setFXResourceCatalog(fxResourceCatalog_);
        if (prepared_)
            backend_->prepare(preparedSampleRate_, preparedMaximumBlockSize_);
    }

    IMASSVoiceBackend* backend() noexcept { return backend_; }
    const IMASSVoiceBackend* backend() const noexcept { return backend_; }

    // Immutable, startup-decoded IC51 catalog; safe for backend read-only lookup.
    void setProgramCatalog(const MASSResourceProgramCatalog* catalog) noexcept {
        programCatalog_ = catalog;
        backend_->setProgramCatalog(programCatalog_);
    }
    const MASSResourceProgramCatalog* programCatalog() const noexcept { return programCatalog_; }

    // Immutable startup-decoded IC4/IC51 FX directory and upload streams.
    // The backend receives both banks without having to parse or allocate
    // resources in the real-time callback.
    void setFXResourceCatalog(const MASSFXResourceCatalog* catalog) noexcept {
        fxResourceCatalog_ = catalog;
        backend_->setFXResourceCatalog(fxResourceCatalog_);
    }
    const MASSFXResourceCatalog* fxResourceCatalog() const noexcept {
        return fxResourceCatalog_;
    }

    void prepare(double sampleRate, int maximumBlockSize) {
        preparedSampleRate_ = sampleRate;
        preparedMaximumBlockSize_ = maximumBlockSize;
        prepared_ = true;
        backend_->prepare(sampleRate, maximumBlockSize);
        voices_.reset();
    }

    void noteOn(std::uint8_t part, std::uint8_t channel,
                std::uint8_t note, std::uint8_t velocity) noexcept {
        // MIDI Note On with velocity zero is Note Off. Route it through the
        // engine so the backend envelope/gate is released as well as the slot.
        if (velocity == 0) {
            noteOff(channel, note);
            return;
        }
        if (part >= MASSPartParameterStore::kPartCount) return;
        const int trackMode = partParameters_.get(static_cast<int>(part), PartParameter::trackMode);
        if (trackMode == 0) return; // Mute
        const auto bankNumber = static_cast<std::uint8_t>(std::max(0, partParameters_.get(static_cast<int>(part), PartParameter::bankNumber)));
        const auto soundNumber = static_cast<std::uint8_t>(std::max(0, partParameters_.get(static_cast<int>(part), PartParameter::soundNumber)));

        int preferredVoice = -1;
        if (trackMode == 2 || trackMode == 3) { // Mono / Lead: one active voice per Part
            const auto& slots = voices_.voices();
            std::uint64_t newestAge = 0;
            for (std::size_t i = 0; i < slots.size(); ++i) {
                if (slots[i].active && slots[i].part == part && slots[i].age >= newestAge) {
                    newestAge = slots[i].age;
                    preferredVoice = static_cast<int>(i);
                }
            }
            // Collapse any older Poly voices for this Part before reusing the newest slot.
            for (std::size_t i = 0; i < slots.size(); ++i) {
                if (slots[i].active && slots[i].part == part && static_cast<int>(i) != preferredVoice) {
                    backend_->resetVoice(static_cast<int>(i), slots[i]);
                    voices_.finishRelease(i);
                }
            }
        }

        const int target = preferredVoice >= 0 ? preferredVoice : voices_.allocationTarget();
        const bool legatoReuse = preferredVoice >= 0 &&
            !voices_.voices()[static_cast<std::size_t>(preferredVoice)].releasing;
        if (preferredVoice < 0 && target >= 0 &&
            voices_.voices()[static_cast<std::size_t>(target)].active &&
            !voices_.voices()[static_cast<std::size_t>(target)].releasing)
            backend_->noteOff(target, voices_.voices()[static_cast<std::size_t>(target)]);
        const int voiceIndex = preferredVoice >= 0
            ? (voices_.retargetVoice(static_cast<std::size_t>(preferredVoice), channel, note, velocity,
                                  bankNumber, soundNumber)
                ? preferredVoice : -1)
            : voices_.noteOn(part, channel, note, velocity, -1, bankNumber, soundNumber);
        if (voiceIndex >= 0 && !legatoReuse)
            backend_->noteOn(voiceIndex, voices_.voices()[static_cast<std::size_t>(voiceIndex)]);
        if ((trackMode == 2 || trackMode == 3) && voiceIndex >= 0)
            pushMonoKey(static_cast<int>(part), channel, note, velocity);
    }

    void noteOff(std::uint8_t channel, std::uint8_t note) noexcept {
        // Mono/Lead use last-note priority. Releasing the current key restores
        // the newest still-held key without restarting the voice envelope.
        std::array<bool, MASSPartParameterStore::kPartCount> handledMonoParts{};
        for (std::size_t part = 0; part < MASSPartParameterStore::kPartCount; ++part) {
            const int mode = partParameters_.get(static_cast<int>(part), PartParameter::trackMode);
            if (mode != 2 && mode != 3) continue;
            auto& count = monoKeyCounts_[part];
            int found = -1;
            for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
                const auto& key = monoKeyStacks_[part][static_cast<std::size_t>(i)];
                if (key.channel == channel && key.note == note && key.keyDown) {
                    found = i;
                    break;
                }
            }
            if (found < 0) continue;
            handledMonoParts[part] = true;
            const bool isTop = static_cast<std::size_t>(found) == count - 1;
            if (isTop && runtimeControls_.sustainPedal(static_cast<int>(part))) {
                monoKeyStacks_[part][static_cast<std::size_t>(found)].keyDown = false;
                monoKeyStacks_[part][static_cast<std::size_t>(found)].sustained = true;
                const int voiceIndex = activeVoiceForPart(static_cast<int>(part));
                if (voiceIndex >= 0 && voices_.voices()[static_cast<std::size_t>(voiceIndex)].note == note)
                    voices_.holdForSustain(static_cast<std::size_t>(voiceIndex));
                continue;
            }

            removeMonoKeyAt(part, static_cast<std::size_t>(found));
            if (!isTop) continue;
            while (count > 0 && !monoKeyStacks_[part][count - 1].keyDown &&
                   !monoKeyStacks_[part][count - 1].sustained)
                --count;

            const int voiceIndex = activeVoiceForPart(static_cast<int>(part));
            if (voiceIndex < 0) continue;
            if (count > 0) {
                const auto& previous = monoKeyStacks_[part][count - 1];
                if (voices_.retargetVoice(static_cast<std::size_t>(voiceIndex),
                                          previous.channel, previous.note, previous.velocity))
                    continue;
            }
            releaseVoiceWithSustain(static_cast<std::size_t>(voiceIndex));
        }

        const auto& slots = voices_.voices();
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const auto& voice = slots[i];
            if (!voice.active || voice.releasing || voice.channel != channel || voice.note != note ||
                handledMonoParts[voice.part])
                continue;
            releaseVoiceWithSustain(i);
        }
    }

    void allNotesOff(int channel = 0) noexcept {
        for (std::size_t part = 0; part < monoKeyCounts_.size(); ++part) {
            auto& count = monoKeyCounts_[part];
            for (std::size_t i = 0; i < count;) {
                if (channel == 0 || monoKeyStacks_[part][i].channel == channel)
                    removeMonoKeyAt(part, i);
                else
                    ++i;
            }
        }
        const auto& slots = voices_.voices();
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const auto& voice = slots[i];
            if (!voice.active || voice.releasing || (channel != 0 && voice.channel != channel))
                continue;
            if (runtimeControls_.sustainPedal(voice.part)) {
                voices_.holdForSustain(i);
            } else {
                backend_->noteOff(static_cast<int>(i), voice);
                voices_.releaseVoice(i);
            }
        }
    }

    void allSoundsOff(int channel = 0) noexcept {
        for (std::size_t part = 0; part < monoKeyCounts_.size(); ++part) {
            auto& count = monoKeyCounts_[part];
            for (std::size_t i = 0; i < count;) {
                if (channel == 0 || monoKeyStacks_[part][i].channel == channel)
                    removeMonoKeyAt(part, i);
                else
                    ++i;
            }
        }
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

    bool setPerformanceNameByte(int index, int value) noexcept { return performanceImage_.setNameByte(index, value); }
    bool setPerformanceCommonByte(int index, int value) noexcept { return performanceImage_.setCommonByte(index, value); }
    bool setPerformancePartByte(int partIndex, int byteIndex, int value) noexcept {
        return performanceImage_.setPartByte(partIndex, byteIndex, value);
    }
    void clearPerformanceImage() noexcept { performanceImage_.clear(); }
    void markPerformanceImageLoaded(bool loaded = true) noexcept { performanceImage_.setLoaded(loaded); }
    const MASSPerformanceImageStore& performanceImage() const noexcept { return performanceImage_; }

    bool setCommonControl(MASSCommonControl control, int value) noexcept { return commonControls_.set(control, value); }
    int getCommonControl(MASSCommonControl control) const noexcept { return commonControls_.get(control); }
    void clearCommonControl(MASSCommonControl control) noexcept { commonControls_.clear(control); }
    void clearCommonModulationDepths() noexcept { commonControls_.clearModulationDepths(); }
    bool setCommonModulationDepth(int sourceIndex, int destinationIndex, int rawValue) noexcept {
        return commonControls_.setModulationDepth(sourceIndex, destinationIndex, rawValue);
    }
    int getCommonModulationDepth(int sourceIndex, int destinationIndex) const noexcept {
        return commonControls_.modulationDepth(sourceIndex, destinationIndex);
    }

    bool setPartParameter(int partIndex, PartParameter parameter, int rawValue) noexcept {
        if (!partParameters_.set(partIndex, parameter, rawValue)) return false;
        if (parameter == PartParameter::trackMode &&
            partIndex >= 0 && partIndex < static_cast<int>(MASSPartParameterStore::kPartCount)) {
            clearMonoKeys(partIndex);
        }
        if (parameter == PartParameter::trackMode && rawValue == 0 &&
            partIndex >= 0 && partIndex < static_cast<int>(MASSPartParameterStore::kPartCount)) {
            const auto& slots = voices_.voices();
            for (std::size_t i = 0; i < slots.size(); ++i) {
                if (slots[i].active && slots[i].part == static_cast<std::uint8_t>(partIndex)) {
                    backend_->resetVoice(static_cast<int>(i), slots[i]);
                    voices_.finishRelease(i);
                }
            }
        }
        if (parameter == PartParameter::holdPedal && partIndex >= 0 &&
            partIndex < static_cast<int>(MASSPartParameterStore::kPartCount)) {
            const bool pedalDown = sustainControllerDown_[static_cast<std::size_t>(partIndex)]
                .load(std::memory_order_acquire);
            setSustainPedal(partIndex, pedalDown);
        }
        return true;
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
    bool setAftertouch(int partIndex, int value) noexcept { return modulationSources_.setAftertouch(partIndex, value); }
    int getAftertouch(int partIndex) const noexcept { return modulationSources_.aftertouch(partIndex); }
    bool setFreeController(int partIndex, int value) noexcept {
        return modulationSources_.setFreeController(partIndex, value);
    }
    int getFreeController(int partIndex) const noexcept {
        return modulationSources_.freeController(partIndex);
    }
    bool setPortamento(int partIndex, bool enabled) noexcept { return runtimeControls_.setPortamento(partIndex, enabled); }
    bool setSoftPedal(int partIndex, bool enabled) noexcept { return runtimeControls_.setSoftPedal(partIndex, enabled); }
    bool setSustainPedal(int partIndex, bool enabled) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(MASSPartParameterStore::kPartCount)) return false;
        const auto part = static_cast<std::size_t>(partIndex);
        sustainControllerDown_[part].store(enabled, std::memory_order_release);
        const bool holdPedalEnabled = partParameters_.get(partIndex, PartParameter::holdPedal) == 1;
        const bool effectiveEnabled = enabled && holdPedalEnabled;
        const bool wasEnabled = runtimeControls_.sustainPedal(partIndex);
        if (!runtimeControls_.setSustainPedal(partIndex, effectiveEnabled)) return false;
        if (wasEnabled && !effectiveEnabled) {
            auto& monoCount = monoKeyCounts_[part];
            for (std::size_t i = 0; i < monoCount;) {
                if (!monoKeyStacks_[part][i].keyDown)
                    removeMonoKeyAt(part, i);
                else
                    ++i;
            }
            const int monoVoice = activeVoiceForPart(partIndex);
            if (monoVoice >= 0 && monoCount > 0) {
                const auto& top = monoKeyStacks_[part][monoCount - 1];
                voices_.retargetVoice(static_cast<std::size_t>(monoVoice),
                                      top.channel, top.note, top.velocity);
            } else if (monoVoice >= 0 && voices_.voices()[static_cast<std::size_t>(monoVoice)].sustained) {
                backend_->noteOff(monoVoice, voices_.voices()[static_cast<std::size_t>(monoVoice)]);
                voices_.releaseVoice(static_cast<std::size_t>(monoVoice));
            }
            const auto& slots = voices_.voices();
            for (std::size_t i = 0; i < slots.size(); ++i) {
                const auto& voice = slots[i];
                if (!voice.active || voice.releasing || voice.part != partIndex ||
                    !voice.sustained || voice.keyDown || static_cast<int>(i) == monoVoice)
                    continue;
                backend_->noteOff(static_cast<int>(i), voice);
                voices_.releaseVoice(i);
            }
        }
        return true;
    }
    bool portamento(int partIndex) const noexcept { return runtimeControls_.portamento(partIndex); }
    bool softPedal(int partIndex) const noexcept { return runtimeControls_.softPedal(partIndex); }
    bool sustainPedal(int partIndex) const noexcept { return runtimeControls_.sustainPedal(partIndex); }
    bool sustainControllerDown(int partIndex) const noexcept {
        return partIndex >= 0 && partIndex < static_cast<int>(MASSPartParameterStore::kPartCount) &&
            sustainControllerDown_[static_cast<std::size_t>(partIndex)].load(std::memory_order_acquire);
    }
    bool setRpn(int partIndex, int parameterIndex, int value) noexcept { return runtimeControls_.setRpn(partIndex, parameterIndex, value); }
    int getRpn(int partIndex, int parameterIndex) const noexcept { return runtimeControls_.rpn(partIndex, parameterIndex); }
    bool setRpnLsb(int partIndex, int parameterIndex, int value) noexcept {
        return runtimeControls_.setRpnLsb(partIndex, parameterIndex, value);
    }
    int getRpnLsb(int partIndex, int parameterIndex) const noexcept {
        return runtimeControls_.rpnLsb(partIndex, parameterIndex);
    }
    void resetPartControllers(int partIndex) noexcept {
        setSustainPedal(partIndex, false);
        runtimeControls_.reset(partIndex);
        modulationSources_.reset(partIndex);
    }

    void render(float* const* channels, int channelCount, int sampleCount) noexcept {
        backend_->render(channels, channelCount, sampleCount, voices_, modulation_, partParameters_,
                        modulationSources_, runtimeControls_, commonControls_, performanceImage_, drumParameters_);
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
    struct MonoKeyEntry {
        std::uint8_t channel = 0;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        bool keyDown = false;
        bool sustained = false;
    };

    static constexpr std::size_t kMonoKeyCapacity = MASSPartParameterStore::kPartCount > VoiceManager::kVoiceCount
        ? MASSPartParameterStore::kPartCount : VoiceManager::kVoiceCount;

    void clearMonoKeys(int partIndex) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(MASSPartParameterStore::kPartCount)) return;
        monoKeyCounts_[static_cast<std::size_t>(partIndex)] = 0;
    }

    void pushMonoKey(int partIndex, std::uint8_t channel, std::uint8_t note,
                     std::uint8_t velocity) noexcept {
        if (partIndex < 0 || partIndex >= static_cast<int>(MASSPartParameterStore::kPartCount)) return;
        auto& count = monoKeyCounts_[static_cast<std::size_t>(partIndex)];
        auto& stack = monoKeyStacks_[static_cast<std::size_t>(partIndex)];
        if (count >= stack.size()) {
            for (std::size_t i = 1; i < count; ++i) stack[i - 1] = stack[i];
            --count;
        }
        stack[count++] = {channel, note, velocity, true, false};
    }

    void removeMonoKeyAt(std::size_t part, std::size_t index) noexcept {
        if (part >= monoKeyStacks_.size() || index >= monoKeyCounts_[part]) return;
        auto& count = monoKeyCounts_[part];
        for (std::size_t i = index + 1; i < count; ++i)
            monoKeyStacks_[part][i - 1] = monoKeyStacks_[part][i];
        --count;
    }

    int activeVoiceForPart(int partIndex) const noexcept {
        int selected = -1;
        std::uint64_t newest = 0;
        const auto& slots = voices_.voices();
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].active && slots[i].part == static_cast<std::uint8_t>(partIndex) &&
                (selected < 0 || slots[i].age >= newest)) {
                selected = static_cast<int>(i);
                newest = slots[i].age;
            }
        return selected;
    }

    void releaseVoiceWithSustain(std::size_t voiceIndex) noexcept {
        if (voiceIndex >= voices_.voices().size()) return;
        const auto& voice = voices_.voices()[voiceIndex];
        if (!voice.active || voice.releasing) return;
        if (runtimeControls_.sustainPedal(voice.part)) {
            voices_.holdForSustain(voiceIndex);
        } else {
            backend_->noteOff(static_cast<int>(voiceIndex), voice);
            voices_.releaseVoice(voiceIndex);
        }
    }

    NullMASSVoiceBackend nullBackend_;
    IMASSVoiceBackend* backend_;
    const MASSResourceProgramCatalog* programCatalog_ = nullptr;
    const MASSFXResourceCatalog* fxResourceCatalog_ = nullptr;
    double preparedSampleRate_ = 44100.0;
    int preparedMaximumBlockSize_ = 0;
    bool prepared_ = false;
    VoiceManager voices_;
    ModulationMatrix modulation_;
    MASSPartParameterStore partParameters_;
    MASSModulationSourceStore modulationSources_;
    MASSPartRuntimeControlStore runtimeControls_;
    MASSCommonControlStore commonControls_;
    MASSPerformanceImageStore performanceImage_;
    MASSDrumParameterStore drumParameters_;
    std::array<std::atomic<bool>, MASSPartParameterStore::kPartCount> sustainControllerDown_{};
    std::array<std::array<MonoKeyEntry, kMonoKeyCapacity>, MASSPartParameterStore::kPartCount> monoKeyStacks_{};
    std::array<std::size_t, MASSPartParameterStore::kPartCount> monoKeyCounts_{};
};

} // namespace technox
