#pragma once
#include <JuceHeader.h>
#include "TechnoXPartModel.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace technox {
struct OpaqueSysExBlock {
    std::uint8_t blockSelector = 0;
    std::vector<std::uint8_t> completeMessage; // original F0...F7 bytes
};
struct Performance {
    std::uint8_t sourceIndex = 0;
    std::string name;
    OpaqueSysExBlock nameBlock;
    OpaqueSysExBlock commonBlock;
    std::array<OpaqueSysExBlock, 4> parts;

    PartEditModel makePartEditModel(std::size_t partIndex) const {
        if (partIndex >= parts.size()) return {};
        return PartEditModel(parts[partIndex].completeMessage);
    }
};

// Parses the embedded Standard MIDI File and retains complete original SysEx frames.
// Unknown payload bytes are deliberately opaque and lossless.
class PerformanceBank {
public:
    static constexpr std::size_t kExpectedPerformances = 50;

    bool loadFromMidi(const void* midiBytes, int byteCount) {
        if (midiBytes == nullptr || byteCount <= 0) return false;
        juce::MemoryInputStream input(midiBytes, static_cast<size_t>(byteCount), false);
        juce::MidiFile midi;
        if (!midi.readFrom(input, false)) return false;

        std::vector<std::vector<std::uint8_t>> frames;
        for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex) {
            const auto* track = midi.getTrack(trackIndex);
            if (track == nullptr) continue;
            for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex) {
                const auto* event = track->getEventPointer(eventIndex);
                if (event == nullptr || !event->message.isSysEx()) continue;
                const auto& message = event->message;
                const auto* payload = message.getSysExData();
                const int payloadSize = message.getSysExDataSize();
                if (payload == nullptr || payloadSize < 0) return false;

                std::vector<std::uint8_t> raw;
                raw.reserve(static_cast<std::size_t>(payloadSize) + 2);
                raw.push_back(0xF0);
                raw.insert(raw.end(), payload, payload + payloadSize);
                raw.push_back(0xF7);
                frames.push_back(std::move(raw));
            }
        }

        if (frames.size() != kExpectedPerformances * 6) return false;
        std::vector<Performance> parsed;
        parsed.reserve(kExpectedPerformances);
        constexpr std::array<std::uint8_t, 5> header{{0xF0, 0x3F, 0x00, 0x22, 0x44}};
        constexpr std::array<std::uint8_t, 6> types{{0x05, 0x00, 0x01, 0x02, 0x03, 0x04}};
        constexpr std::array<std::size_t, 6> sizes{{16, 73, 32, 32, 32, 32}};

        for (std::size_t record = 0; record < kExpectedPerformances; ++record) {
            const auto base = record * 6;
            const auto& nameFrame = frames[base];
            if (nameFrame.size() != sizes[0]) return false;
            const auto sourceIndex = nameFrame[5];
            for (std::size_t block = 0; block < 6; ++block) {
                const auto& frame = frames[base + block];
                if (frame.size() != sizes[block] || frame.back() != 0xF7) return false;
                if (!std::equal(header.begin(), header.end(), frame.begin())) return false;
                if (frame[5] != sourceIndex || frame[6] != types[block]) return false;
                for (std::size_t i = 1; i + 1 < frame.size(); ++i)
                    if (frame[i] > 0x7F) return false;
            }
            if (sourceIndex != static_cast<std::uint8_t>(record + 5)) return false;

            Performance p;
            p.sourceIndex = sourceIndex;
            p.name.assign(nameFrame.begin() + 7, nameFrame.end() - 1);
            while (!p.name.empty() && (p.name.back() == ' ' || p.name.back() == '\0'))
                p.name.pop_back();
            p.nameBlock = {types[0], nameFrame};
            p.commonBlock = {types[1], frames[base + 1]};
            for (std::size_t part = 0; part < 4; ++part)
                p.parts[part] = {types[part + 2], frames[base + part + 2]};
            parsed.push_back(std::move(p));
        }
        records_ = std::move(parsed);
        selected_.store(0, std::memory_order_release);
        return true;
    }

    std::size_t count() const noexcept { return records_.size(); }
    std::size_t selectedIndex() const noexcept { return selected_.load(std::memory_order_acquire); }
    const Performance* current() const noexcept {
        const auto index = selectedIndex();
        return index < records_.size() ? &records_[index] : nullptr;
    }
    const Performance* at(std::size_t i) const noexcept {
        return i < records_.size() ? &records_[i] : nullptr;
    }
    bool select(std::size_t i) noexcept {
        if (i >= records_.size()) return false;
        selected_.store(i, std::memory_order_release);
        return true;
    }
    bool selectSourceIndex(std::uint8_t id) noexcept {
        for (std::size_t i = 0; i < records_.size(); ++i)
            if (records_[i].sourceIndex == id) return select(i);
        return false;
    }

private:
    std::vector<Performance> records_;
    std::atomic<std::size_t> selected_{0};
};
} // namespace technox
