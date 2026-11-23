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
    // IC51 program identity captured at note-on for the DSP backend.
    std::uint8_t bankNumber = 0;
    std::uint8_t soundNumber = 0;
    std::uint64_t age = 0;
    bool keyDown = true;
    bool sustained = false;
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

    int noteOn(std::uint8_t part, std::uint8_t channel, std::uint8_t note, std::uint8_t velocity,
               int preferredVoiceIndex = -1, std::uint8_t bankNumber = 0,
               std::uint8_t soundNumber = 0) noexcept {
        if (velocity == 0) {
            noteOff(channel, note);
            return -1;
        }
        int selected = preferredVoiceIndex >= 0 &&
            preferredVoiceIndex < static_cast<int>(voices_.size())
                ? preferredVoiceIndex : -1;
        if (selected < 0) {
            for (std::size_t i = 0; i < voices_.size(); ++i) {
                if (!voices_[i].active) { selected = static_cast<int>(i); break; }
            }
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
        voice = {};
        voice.active = true;
        voice.part = part;
        voice.channel = channel;
        voice.note = note;
        voice.velocity = velocity;
        voice.bankNumber = bankNumber;
        voice.soundNumber = soundNumber;
        voice.age = ++age_;
        voice.keyDown = true;
        return selected;
    }

    bool retargetVoice(std::size_t voiceIndex, std::uint8_t channel,
                       std::uint8_t note, std::uint8_t velocity) noexcept {
        if (voiceIndex >= voices_.size() || !voices_[voiceIndex].active || velocity == 0)
            return false;
        auto& voice = voices_[voiceIndex];
        voice.channel = channel;
        voice.note = note;
        voice.velocity = velocity;
        voice.age = ++age_;
        voice.keyDown = true;
        voice.sustained = false;
        voice.releasing = false;
        return true;
    }

    bool retargetVoice(std::size_t voiceIndex, std::uint8_t channel,
                       std::uint8_t note, std::uint8_t velocity,
                       std::uint8_t bankNumber, std::uint8_t soundNumber) noexcept {
        if (!retargetVoice(voiceIndex, channel, note, velocity)) return false;
        voices_[voiceIndex].bankNumber = bankNumber;
        voices_[voiceIndex].soundNumber = soundNumber;
        return true;
    }

    void noteOff(std::uint8_t channel, std::uint8_t note) noexcept {
        for (std::size_t i = 0; i < voices_.size(); ++i)
            if (voices_[i].active && voices_[i].channel == channel && voices_[i].note == note)
                releaseVoice(i);
    }

    void allNotesOff(int channel = 0) noexcept {
        for (std::size_t i = 0; i < voices_.size(); ++i)
            if (voices_[i].active && (channel == 0 || voices_[i].channel == channel))
                releaseVoice(i);
    }

    void holdForSustain(std::size_t voiceIndex) noexcept {
        if (voiceIndex >= voices_.size() || !voices_[voiceIndex].active) return;
        auto& voice = voices_[voiceIndex];
        voice.keyDown = false;
        voice.sustained = true;
    }

    void releaseVoice(std::size_t voiceIndex) noexcept {
        if (voiceIndex >= voices_.size() || !voices_[voiceIndex].active) return;
        auto& voice = voices_[voiceIndex];
        voice.keyDown = false;
        voice.sustained = false;
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
