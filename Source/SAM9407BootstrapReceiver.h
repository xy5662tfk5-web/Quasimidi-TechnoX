#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace technox {

// Datasheet-level model of the SAM9407's power-up bootstrap boundary.
// This is deliberately separate from the TechnoX IC4 0x8000-0x8003 bus:
// callers must supply already-decoded 16-bit host burst words.
class SAM9407BootstrapReceiver {
public:
    enum class State : std::uint8_t {
        awaitingBootstrapWords,
        executingExternalMemory
    };

    static constexpr std::uint32_t kExternalEntryAddress = 0x0100u;
    static constexpr std::size_t kBootstrapWordCount = 256u;
    static constexpr std::uint32_t kBootstrapEndAddress =
        kExternalEntryAddress + static_cast<std::uint32_t>(kBootstrapWordCount);

    // BOOT is sampled at reset. BOOT=1 selects the internal bootstrap ROM;
    // BOOT=0 starts P16 execution directly from external address 0x0100.
    void reset(bool bootPinHigh) noexcept {
        bootstrapMemory_.fill(0);
        receivedWordCount_ = 0;
        bootPinHigh_ = bootPinHigh;
        state_ = bootPinHigh
            ? State::awaitingBootstrapWords
            : State::executingExternalMemory;
    }

    // Accept one 16-bit word from the abstracted host burst port.
    // Host address/strobe decode and electrical timing are intentionally
    // outside this component and must be modeled by a separately verified bridge.
    bool writeBurstWord(std::uint16_t word) noexcept {
        if (state_ != State::awaitingBootstrapWords ||
            receivedWordCount_ >= bootstrapMemory_.size())
            return false;

        bootstrapMemory_[receivedWordCount_++] = word;
        if (receivedWordCount_ == kBootstrapWordCount)
            state_ = State::executingExternalMemory;
        return true;
    }

    // Read only words actually received through the bootstrap path. Addresses
    // are 16-bit word addresses, not byte offsets.
    bool readBootstrapWord(std::uint32_t wordAddress,
                           std::uint16_t& value) const noexcept {
        if (wordAddress < kExternalEntryAddress ||
            wordAddress >= kExternalEntryAddress + receivedWordCount_)
            return false;

        value = bootstrapMemory_[
            static_cast<std::size_t>(wordAddress - kExternalEntryAddress)];
        return true;
    }

    State state() const noexcept { return state_; }
    bool bootPinWasHigh() const noexcept { return bootPinHigh_; }
    bool isAwaitingBootstrapWords() const noexcept {
        return state_ == State::awaitingBootstrapWords;
    }
    bool isExecutingExternalMemory() const noexcept {
        return state_ == State::executingExternalMemory;
    }
    std::size_t receivedWordCount() const noexcept { return receivedWordCount_; }

    // The external entry point is meaningful only after the selected boot
    // path has reached external-memory execution.
    bool getEntryProgramCounter(std::uint32_t& address) const noexcept {
        if (!isExecutingExternalMemory())
            return false;
        address = kExternalEntryAddress;
        return true;
    }

private:
    std::array<std::uint16_t, kBootstrapWordCount> bootstrapMemory_{};
    std::size_t receivedWordCount_ = 0;
    bool bootPinHigh_ = false;
    State state_ = State::executingExternalMemory;
};

} // namespace technox
