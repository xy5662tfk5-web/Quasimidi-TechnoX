#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace technox {
struct OpaqueSysExBlock {
    std::uint8_t type = 0;
    std::vector<std::uint8_t> completeMessage; // original F0...F7 bytes
};
struct Performance {
    std::uint8_t sourceIndex = 0;
    std::string name;
    OpaqueSysExBlock nameBlock;
    OpaqueSysExBlock commonBlock;
    std::array<OpaqueSysExBlock, 4> parts;
};

// UI-facing selection model. Parsing is performed by the MIDI/SysEx importer;
// this class never modifies opaque message bytes.
class PerformanceBank {
public:
    static constexpr std::size_t kExpectedPerformances = 50;
    bool load(std::vector<Performance> records) {
        if (records.size() != kExpectedPerformances) return false;
        for (const auto& p : records) {
            if (p.nameBlock.completeMessage.empty() || p.commonBlock.completeMessage.empty()) return false;
            for (const auto& part : p.parts) if (part.completeMessage.empty()) return false;
        }
        records_ = std::move(records); selected_ = 0; return true;
    }
    std::size_t count() const noexcept { return records_.size(); }
    std::size_t selectedIndex() const noexcept { return selected_; }
    const Performance* current() const noexcept { return records_.empty() ? nullptr : &records_[selected_]; }
    const Performance* at(std::size_t i) const noexcept { return i < records_.size() ? &records_[i] : nullptr; }
    bool select(std::size_t i) noexcept { if (i >= records_.size()) return false; selected_ = i; return true; }
    bool selectSourceIndex(std::uint8_t id) noexcept {
        for (std::size_t i=0; i<records_.size(); ++i) if (records_[i].sourceIndex == id) return select(i);
        return false;
    }
private:
    std::vector<Performance> records_;
    std::size_t selected_ = 0;
};
} // namespace technox
