#pragma once

#include "TechnoXPartModel.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace technox {

// Latest-value mailbox for GUI-originated Part edits.
// One producer (the editor/message thread), one consumer (processBlock).
// Repeated drags of the same control coalesce to the newest value, so an
// idle/stopped audio callback cannot cause an unbounded event backlog.
class PanelPartEditMailbox {
public:
    static constexpr std::size_t kPerformanceCount = 50;
    static constexpr std::size_t kPartCount = 4;
    static constexpr std::size_t kParameterCount = PartEditModel::kDataSize;
    static constexpr std::size_t kEditCount = kPartCount * kParameterCount;
    static constexpr std::size_t kWordCount = (kEditCount + 31u) / 32u;

    struct Edit {
        int partIndex = -1;
        PartParameter parameter = PartParameter::bankNumber;
        int rawValue = -1;
    };

    PanelPartEditMailbox() noexcept {
        for (auto& performance : pendingValues_)
            for (auto& value : performance)
                value.store(-1, std::memory_order_relaxed);
        for (auto& performance : dirtyWords_)
            for (auto& word : performance)
                word.store(0, std::memory_order_relaxed);
    }

    bool publish(int performanceIndex, int partIndex,
                 PartParameter parameter, int rawValue) noexcept {
        const auto parameterIndex = static_cast<std::size_t>(parameter);
        const auto* descriptor = PartEditModel::descriptor(parameterIndex);
        if (!validPerformance(performanceIndex) ||
            partIndex < 0 || partIndex >= static_cast<int>(kPartCount) ||
            descriptor == nullptr || rawValue < 0 ||
            rawValue > descriptor->maximum)
            return false;

        const std::size_t editIndex =
            static_cast<std::size_t>(partIndex) * kParameterCount + parameterIndex;
        const std::size_t wordIndex = editIndex / 32u;
        const std::uint32_t bit = std::uint32_t{1} << (editIndex % 32u);

        pendingValues_[static_cast<std::size_t>(performanceIndex)][editIndex]
            .store(rawValue, std::memory_order_release);
        dirtyWords_[static_cast<std::size_t>(performanceIndex)][wordIndex]
            .fetch_or(bit, std::memory_order_release);
        return true;
    }

    template <typename Consumer>
    void consume(int performanceIndex, Consumer&& consumer) noexcept {
        if (!validPerformance(performanceIndex))
            return;

        const auto performance = static_cast<std::size_t>(performanceIndex);
        for (std::size_t wordIndex = 0; wordIndex < kWordCount; ++wordIndex) {
            std::uint32_t pending = dirtyWords_[performance][wordIndex]
                .exchange(0, std::memory_order_acq_rel);

            while (pending != 0) {
                const std::uint32_t lowestBit = pending & (~pending + 1u);
                pending &= (pending - 1u);

                std::size_t bitIndex = 0;
                std::uint32_t scan = lowestBit;
                while ((scan & 1u) == 0u) {
                    scan >>= 1u;
                    ++bitIndex;
                }

                const std::size_t editIndex = wordIndex * 32u + bitIndex;
                if (editIndex >= kEditCount)
                    continue;

                const int value = pendingValues_[performance][editIndex]
                    .load(std::memory_order_acquire);
                const auto part = static_cast<int>(editIndex / kParameterCount);
                const auto parameter = static_cast<PartParameter>(
                    editIndex % kParameterCount);
                if (value >= 0)
                    consumer(Edit{part, parameter, value});
            }
        }
    }

    void clear(int performanceIndex) noexcept {
        if (!validPerformance(performanceIndex))
            return;
        for (auto& word : dirtyWords_[static_cast<std::size_t>(performanceIndex)])
            word.store(0, std::memory_order_release);
    }

private:
    static constexpr bool validPerformance(int index) noexcept {
        return index >= 0 && index < static_cast<int>(kPerformanceCount);
    }

    std::array<std::array<std::atomic<int>, kEditCount>, kPerformanceCount>
        pendingValues_{};
    std::array<std::array<std::atomic<std::uint32_t>, kWordCount>, kPerformanceCount>
        dirtyWords_{};
};

} // namespace technox
