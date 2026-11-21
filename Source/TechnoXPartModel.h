#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace technox {

// Conservative editable view of one observed 33-byte Part SysEx message.
// Only bank/program are provisionally interpreted; other bytes stay opaque.
class PartEditModel {
public:
    static constexpr std::size_t kFrameSize = 33;
    static constexpr std::size_t kDataOffset = 7;
    static constexpr std::size_t kDataSize = 25;
    static constexpr std::size_t kBankOffset = 1;
    static constexpr std::size_t kProgramOffset = 2;

    bool load(const std::vector<std::uint8_t>& message) {
        if (!isValidFrame(message)) return false;
        original_ = message;
        edited_ = message;
        dirty_ = false;
        return true;
    }

    bool isLoaded() const noexcept { return !original_.empty(); }
    bool isDirty() const noexcept { return dirty_; }
    std::uint8_t sourcePerformanceIndex() const noexcept { return isLoaded() ? original_[5] : 0; }
    std::uint8_t partNumber() const noexcept { return isLoaded() ? original_[6] : 0; }

    // Candidate mapping inferred from the .ins banks and repeated Part data.
    int bankCode() const noexcept { return isLoaded() ? edited_[kDataOffset + kBankOffset] : -1; }
    int programNumber() const noexcept { return isLoaded() ? edited_[kDataOffset + kProgramOffset] : -1; }
    bool setBankCode(std::uint8_t value) noexcept {
        if (!isLoaded() || value > 3) return false;
        edited_[kDataOffset + kBankOffset] = value;
        updateDirty();
        return true;
    }
    bool setProgramNumber(std::uint8_t value) noexcept {
        if (!isLoaded() || value > 127) return false;
        edited_[kDataOffset + kProgramOffset] = value;
        updateDirty();
        return true;
    }

    // Access unknown fields without assigning invented parameter names.
    int rawDataByte(std::size_t index) const noexcept {
        if (!isLoaded() || index >= kDataSize) return -1;
        return edited_[kDataOffset + index];
    }
    bool setRawDataByte(std::size_t index, std::uint8_t value) noexcept {
        if (!isLoaded() || index >= kDataSize || value > 0x7F) return false;
        edited_[kDataOffset + index] = value;
        updateDirty();
        return true;
    }

    const std::vector<std::uint8_t>& originalSysEx() const noexcept { return original_; }
    const std::vector<std::uint8_t>& editedSysEx() const noexcept { return edited_; }
    void revert() { edited_ = original_; dirty_ = false; }

private:
    void updateDirty() noexcept { dirty_ = edited_ != original_; }

    static bool isValidFrame(const std::vector<std::uint8_t>& m) noexcept {
        if (m.size() != kFrameSize || m.front() != 0xF0 || m.back() != 0xF7)
            return false;
        constexpr std::array<std::uint8_t, 5> header{{0xF0, 0x3F, 0x00, 0x22, 0x44}};
        for (std::size_t i = 0; i < header.size(); ++i)
            if (m[i] != header[i]) return false;
        if (m[5] < 5 || m[5] > 54 || m[6] < 1 || m[6] > 4)
            return false;
        for (std::size_t i = 1; i + 1 < m.size(); ++i)
            if (m[i] > 0x7F) return false;
        return true;
    }

    std::vector<std::uint8_t> original_;
    std::vector<std::uint8_t> edited_;
    bool dirty_ = false;
};

} // namespace technox
