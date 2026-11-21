#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace technox {

struct VoiceSlot {
    bool active = false;
    bool releasing = false;
    std::uint8_t part = 0;
    std::uint8_t channel = 0;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint64_t age = 0;
};

class VoiceManager {
public:
    static constexpr std::size_t kVoiceCount = 21;

    void reset() noexcept {
        voices_.fill({});
        age_ = 0;
    }

    int allocationTarget() const noexcept {
        for (std::size_t i = 0; i < voices_.size(); ++i)
            if (!voices_[i].active) return static_cast<int>(i);
        int selected = -1;
        std::uint64_t oldest = ~std::uint64_t{0};
        for (std::size_t i = 0; i < voices_.size(); ++i)
            if (voices_[i].releasing && voices_[i].age < oldest) {
                oldest = voices_[i].age;
                selected = static_cast<int>(i);
            }
        if (selected >= 0) return selected;
        for (std::size_t i = 0; i < voices_.size(); ++i)
            if (voices_[i].age < oldest) {
                oldest = voices_[i].age;
                selected = static_cast<int>(i);
            }
        return selected;
    }

    int noteOn(std::uint8_t part, std::uint8_t channel, std::uint8_t note, std::uint8_t velocity) noexcept {
        if (velocity == 0) {
            noteOff(channel, note);
            return -1;
        }
        int selected = -1;
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            if (!voices_[i].active) { selected = static_cast<int>(i); break; }
        }
        if (selected < 0) {
            // Prefer stealing the oldest releasing voice, otherwise the oldest voice.
            std::uint64_t oldest = ~std::uint64_t{0};
            for (std::size_t i = 0; i < voices_.size(); ++i) {
                if (voices_[i].releasing && voices_[i].age < oldest) {
                    oldest = voices_[i].age;
                    selected = static_cast<int>(i);
                }
            }
            if (selected < 0) {
                for (std::size_t i = 0; i < voices_.size(); ++i)
                    if (voices_[i].age < oldest) { oldest = voices_[i].age; selected = static_cast<int>(i); }
            }
        }
        auto& voice = voices_[static_cast<std::size_t>(selected)];
        voice = {true, false, part, channel, note, velocity, ++age_};
        return selected;
    }

    void noteOff(std::uint8_t channel, std::uint8_t note) noexcept {
        for (auto& voice : voices_)
            if (voice.active && voice.channel == channel && voice.note == note)
                voice.releasing = true;
    }

    void allNotesOff(int channel = 0) noexcept {
        for (auto& voice : voices_)
            if (voice.active && (channel == 0 || voice.channel == channel))
                voice.releasing = true;
    }

    void finishRelease(std::size_t voiceIndex) noexcept {
        if (voiceIndex < voices_.size())
            voices_[voiceIndex] = {};
    }

    const std::array<VoiceSlot, kVoiceCount>& voices() const noexcept { return voices_; }
    std::size_t activeVoiceCount() const noexcept {
        std::size_t count = 0;
        for (const auto& voice : voices_) if (voice.active) ++count;
        return count;
    }

private:
    std::array<VoiceSlot, kVoiceCount> voices_{};
    std::uint64_t age_ = 0;
};

} // namespace technox
