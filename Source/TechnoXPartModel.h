#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace technox {

enum class PartParameter : std::size_t {
    bankNumber = 0, soundNumber, trackMode, level, panorama, fx1Send, fx2Send,
    transpose, tune, cutoffFrequency, resonance, egAttack, egDecay, egRelease,
    vibratoRate, vibratoDepth, vibratoDelay, velocityCurve, holdPedal,
    modulationDepth, pitchSensitivity, volumeModSensitivity, toneModSensitivity,
    portamentoTime
};

struct PartParameterDescriptor {
    const char* name;
    std::uint8_t maximum;
    int displayOffset;
};

inline constexpr std::array<PartParameterDescriptor, 24> kPartParameterDescriptors{{
    {"Bank", 3, 0}, {"Sound", 127, 0}, {"Track mode", 3, 0}, {"Level", 127, 0},
    {"Panorama", 20, 0}, {"FX1 send", 63, 0}, {"FX2 send", 63, 0},
    {"Transpose", 48, -24}, {"Tune", 127, -64}, {"Cutoff", 127, -64},
    {"Resonance", 127, -64}, {"EG attack", 127, -64}, {"EG decay", 127, -64},
    {"EG release", 127, -64}, {"Vibrato rate", 127, -64}, {"Vibrato depth", 127, -64},
    {"Vibrato delay", 127, -64}, {"Velocity curve", 14, 0}, {"Hold pedal", 1, 0},
    {"Modulation depth", 127, 0}, {"Pitch sensitivity", 24, -12},
    {"Volume modulation sensitivity", 127, -64}, {"Tone modulation sensitivity", 127, -64},
    {"Portamento time", 127, 0}
}};

// Lossless editor model for one documented 33-byte Part SysEx frame.
// The reserved byte at frame offset 7 is skipped; manual offsets 00..17 map to frame bytes 8..31.
class PartEditModel {
public:
    static constexpr std::size_t kFrameSize = 33;
    static constexpr std::size_t kDataOffset = 8;
    static constexpr std::size_t kDataSize = 24;

    PartEditModel() = default;
    explicit PartEditModel(std::vector<std::uint8_t> completeMessage) {
        load(std::move(completeMessage));
    }

    bool load(std::vector<std::uint8_t> message) {
        if (!isValidFrame(message)) return false;
        original_ = std::move(message);
        edited_ = original_;
        dirty_ = false;
        return true;
    }

    bool isLoaded() const noexcept { return !original_.empty(); }
    bool isDirty() const noexcept { return dirty_; }
    std::size_t sourcePerformanceIndex() const noexcept { return isLoaded() ? edited_[5] : 0; }
    std::size_t partSlotIndex() const noexcept { return isLoaded() ? edited_[6] : 0; }
    // The factory Performance dump stores its four editable Part blocks as parts 13–16.
    std::size_t partNumber() const noexcept {
        return isLoaded() && edited_[6] >= 1 && edited_[6] <= 4 ? 12 + edited_[6] : 0;
    }

    int rawValue(std::size_t index) const noexcept {
        if (!isLoaded() || index >= kDataSize) return -1;
        return static_cast<int>(edited_[kDataOffset + index]);
    }

    int parameterValue(PartParameter parameter) const noexcept {
        const auto index = static_cast<std::size_t>(parameter);
        const int raw = rawValue(index);
        return raw < 0 ? raw : raw + kPartParameterDescriptors[index].displayOffset;
    }

    bool setRawValue(std::size_t index, std::uint8_t value) noexcept {
        if (!isLoaded() || index >= kDataSize || value > 0x7F) return false;
        edited_[kDataOffset + index] = value;
        dirty_ = edited_ != original_;
        return true;
    }

    bool setParameterValue(PartParameter parameter, std::uint8_t value) noexcept {
        const auto index = static_cast<std::size_t>(parameter);
        if (index >= kPartParameterDescriptors.size() ||
            value > kPartParameterDescriptors[index].maximum)
            return false;
        return setRawValue(index, value);
    }

    // UI-facing signed/offset values are converted back to the stored 7-bit value.
    bool setDisplayValue(PartParameter parameter, int displayValue) noexcept {
        const auto index = static_cast<std::size_t>(parameter);
        if (index >= kPartParameterDescriptors.size())
            return false;
        const int raw = displayValue - kPartParameterDescriptors[index].displayOffset;
        if (raw < 0 || raw > kPartParameterDescriptors[index].maximum)
            return false;
        return setRawValue(index, static_cast<std::uint8_t>(raw));
    }

    static constexpr const PartParameterDescriptor* descriptor(std::size_t index) noexcept {
        return index < kPartParameterDescriptors.size() ? &kPartParameterDescriptors[index] : nullptr;
    }

    const std::vector<std::uint8_t>& originalSysEx() const noexcept { return original_; }
    const std::vector<std::uint8_t>& editedSysEx() const noexcept { return edited_; }

    void revert() {
        edited_ = original_;
        dirty_ = false;
    }

private:
    static bool isValidFrame(const std::vector<std::uint8_t>& m) noexcept {
        if (m.size() != kFrameSize || m.front() != 0xF0 || m.back() != 0xF7)
            return false;
        constexpr std::array<std::uint8_t, 5> header{{0xF0, 0x3F, 0x00, 0x22, 0x44}};
        for (std::size_t i = 0; i < header.size(); ++i)
            if (m[i] != header[i]) return false;
        if (m[5] < 5 || m[5] > 54 || m[6] < 0x01 || m[6] > 0x04) return false;
        for (std::size_t i = 1; i + 1 < m.size(); ++i)
            if (m[i] > 0x7F) return false;
        return true;
    }

    std::vector<std::uint8_t> original_;
    std::vector<std::uint8_t> edited_;
    bool dirty_ = false;
};

} // namespace technox
