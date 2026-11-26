#pragma once

#include <cstddef>
#include <cstdint>

namespace technox {

// Non-owning immutable view over a captured external waveform-memory image.
// The owner must keep the bytes alive and unchanged while the audio backend
// can access this view. No codec, sample rate, byte order, address unit,
// banking or loop semantics are implied by this container.
class MASSWaveformMemoryView {
public:
    bool attach(const std::uint8_t* bytes, std::size_t byteCount) noexcept {
        if (bytes == nullptr || byteCount == 0) {
            detach();
            return false;
        }
        bytes_ = bytes;
        byteCount_ = byteCount;
        return true;
    }

    void detach() noexcept {
        bytes_ = nullptr;
        byteCount_ = 0;
    }

    bool isAttached() const noexcept {
        return bytes_ != nullptr && byteCount_ != 0;
    }

    std::size_t sizeInBytes() const noexcept {
        return byteCount_;
    }

    const std::uint8_t* data() const noexcept {
        return bytes_;
    }

    bool readByte(std::size_t address, std::uint8_t& value) const noexcept {
        if (!isAttached() || address >= byteCount_)
            return false;
        value = bytes_[address];
        return true;
    }

    // Returns a stable pointer only for an entirely in-range span. A zero
    // length span at the end of the image is valid and returns one-past-end.
    const std::uint8_t* dataAt(
        std::size_t address, std::size_t length) const noexcept {
        if (!isAttached() || address > byteCount_ ||
            length > byteCount_ - address)
            return nullptr;
        return bytes_ + address;
    }

private:
    const std::uint8_t* bytes_ = nullptr;
    std::size_t byteCount_ = 0;
};

} // namespace technox
