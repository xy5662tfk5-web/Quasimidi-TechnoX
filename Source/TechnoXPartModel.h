#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace technox {

// Conservative editing model for one 32-byte Part SysEx frame.
// Until a byte-to-parameter map is verified, bytes are exposed only as raw 7-bit data.
class PartEditModel {
public:
    static constexpr std::size_t kFrameSize = 32;
    static constexpr std::size_t kDataOffset = 7;
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
    std::size_t partType() const noexcept { return isLoaded() ? edited_[5] : 0; }
    std::size_t sourcePerformanceIndex() const noexcept { return isLoaded() ? edited_[6] : 0; }

    // Zero-based index into the 24 opaque data bytes; -1 means unavailable.
    int rawValue(std::size_t index) const noexcept {
        if (!isLoaded() || index >= kDataSize) return -1;
        return static_cast<int>(edited_[kDataOffset + index]);
    }

    bool setRawValue(std::size_t index, std::uint8_t value) noexcept {
        if (!isLoaded() || index >= kDataSize || value > 0x7F) return false;
        edited_[kDataOffset + index] = value;
        dirty_ = edited_ != original_;
        return true;
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
        if (m[5] < 0x01 || m[5] > 0x04) return false;
        for (std::size_t i = 1; i + 1 < m.size(); ++i)
            if (m[i] > 0x7F) return false;
        return true;
    }

    std::vector<std::uint8_t> original_;
    std::vector<std::uint8_t> edited_;
    bool dirty_ = false;
};

} // namespace technox
