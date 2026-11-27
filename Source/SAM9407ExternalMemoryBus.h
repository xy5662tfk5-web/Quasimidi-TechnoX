#pragma once

#include "MASSWaveformMemoryView.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace technox {

// The SAM9407 drives a 25-bit WORD address bus (WA0-WA24). S1/S0 classify
// the external-memory transaction; they do not select the memory device.
enum class SAM9407MemoryCycle : std::uint8_t {
    idle = 0b00,
    synthesisAccess = 0b01,
    instructionFetch = 0b10,
    processorReadWrite = 0b11
};

enum class SAM9407MemoryDevice : std::uint8_t {
    pcmRom,
    sram,
    dram
};

class SAM9407ExternalMemoryBus {
public:
    static constexpr std::uint32_t kMaximumWordAddress = 0x01ffffffu;
    static constexpr std::uint64_t kAddressSpaceWordCount = 0x02000000ull;
    static constexpr std::size_t kMaximumRegions = 8u;

    // Decode the physical S1/S0 pins. S1 is the high bit.
    static constexpr SAM9407MemoryCycle decodeCycle(bool s1, bool s0) noexcept {
        return static_cast<SAM9407MemoryCycle>(
            (s1 ? 0b10u : 0u) | (s0 ? 0b01u : 0u));
    }

    static constexpr bool encodeS1(SAM9407MemoryCycle cycle) noexcept {
        return (static_cast<std::uint8_t>(cycle) & 0b10u) != 0;
    }

    static constexpr bool encodeS0(SAM9407MemoryCycle cycle) noexcept {
        return (static_cast<std::uint8_t>(cycle) & 0b01u) != 0;
    }

    void clear() noexcept {
        regions_ = {};
        regionCount_ = 0;
    }

    std::size_t regionCount() const noexcept { return regionCount_; }

    bool addReadOnlyRegion(std::uint32_t baseWordAddress,
                           std::uint32_t wordCount,
                           SAM9407MemoryDevice device,
                           const MASSWaveformMemoryView& memory) noexcept {
        if (device != SAM9407MemoryDevice::pcmRom ||
            !memory.isAttached() ||
            !validRange(baseWordAddress, wordCount) ||
            static_cast<std::uint64_t>(wordCount) * 2u >
                memory.sizeInBytes())
            return false;

        Region region{};
        region.baseWordAddress = baseWordAddress;
        region.wordCount = wordCount;
        region.device = device;
        region.readOnlyView = &memory;
        return addRegion(region);
    }

    // Writable backing is supplied by the owner and must remain alive while
    // this bus can be accessed. RAM size is measured in 16-bit words.
    bool addWritableRegion(std::uint32_t baseWordAddress,
                           std::uint32_t wordCount,
                           SAM9407MemoryDevice device,
                           std::uint8_t* bytes,
                           std::size_t byteCount) noexcept {
        if ((device != SAM9407MemoryDevice::sram &&
             device != SAM9407MemoryDevice::dram) ||
            bytes == nullptr ||
            !validRange(baseWordAddress, wordCount) ||
            static_cast<std::uint64_t>(wordCount) * 2u > byteCount)
            return false;

        Region region{};
        region.baseWordAddress = baseWordAddress;
        region.wordCount = wordCount;
        region.device = device;
        region.writableBytes = bytes;
        region.writableByteCount = byteCount;
        return addRegion(region);
    }

    bool containsReadableRange(std::uint32_t firstWordAddress,
                               std::uint32_t endWordAddressExclusive) const noexcept {
        if (firstWordAddress >= endWordAddressExclusive ||
            endWordAddressExclusive > kAddressSpaceWordCount)
            return false;

        for (std::size_t i = 0; i < regionCount_; ++i) {
            const auto& region = regions_[i];
            const auto regionEnd =
                static_cast<std::uint64_t>(region.baseWordAddress) +
                region.wordCount;
            if (firstWordAddress >= region.baseWordAddress &&
                static_cast<std::uint64_t>(endWordAddressExclusive) <= regionEnd &&
                regionIsReadable(region))
                return true;
        }
        return false;
    }

    // WD0-WD15 are treated as a little-endian 16-bit memory word in the
    // backing byte image. The voice backend can byte-swap this word if a
    // separately verified sample format requires it.
    bool readWord(std::uint32_t wordAddress,
                  SAM9407MemoryCycle cycle,
                  std::uint16_t& value) const noexcept {
        if (cycle == SAM9407MemoryCycle::idle ||
            wordAddress > kMaximumWordAddress)
            return false;

        const Region* region = findRegion(wordAddress);
        if (region == nullptr || !regionIsReadable(*region))
            return false;

        const auto byteOffset = static_cast<std::size_t>(
            static_cast<std::uint64_t>(wordAddress - region->baseWordAddress) * 2u);
        std::uint8_t low = 0;
        std::uint8_t high = 0;
        if (region->readOnlyView != nullptr) {
            if (!region->readOnlyView->readByte(byteOffset, low) ||
                !region->readOnlyView->readByte(byteOffset + 1u, high))
                return false;
        } else {
            if (byteOffset + 1u >= region->writableByteCount)
                return false;
            low = region->writableBytes[byteOffset];
            high = region->writableBytes[byteOffset + 1u];
        }

        value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(low) |
            (static_cast<std::uint16_t>(high) << 8));
        return true;
    }

    bool writeWord(std::uint32_t wordAddress,
                   SAM9407MemoryCycle cycle,
                   std::uint16_t value) noexcept {
        // The two documented write-capable cycle classes are synthesis access
        // (01) and P16 processor read/write (11). Instruction fetch is read-only.
        if ((cycle != SAM9407MemoryCycle::synthesisAccess &&
             cycle != SAM9407MemoryCycle::processorReadWrite) ||
            wordAddress > kMaximumWordAddress)
            return false;

        Region* region = findRegion(wordAddress);
        if (region == nullptr || region->writableBytes == nullptr ||
            (region->device != SAM9407MemoryDevice::sram &&
             region->device != SAM9407MemoryDevice::dram))
            return false;

        const auto byteOffset = static_cast<std::size_t>(
            static_cast<std::uint64_t>(wordAddress - region->baseWordAddress) * 2u);
        if (byteOffset + 1u >= region->writableByteCount)
            return false;

        region->writableBytes[byteOffset] =
            static_cast<std::uint8_t>(value & 0xffu);
        region->writableBytes[byteOffset + 1u] =
            static_cast<std::uint8_t>(value >> 8);
        return true;
    }

private:
    struct Region {
        std::uint32_t baseWordAddress = 0;
        std::uint32_t wordCount = 0;
        SAM9407MemoryDevice device = SAM9407MemoryDevice::pcmRom;
        const MASSWaveformMemoryView* readOnlyView = nullptr;
        std::uint8_t* writableBytes = nullptr;
        std::size_t writableByteCount = 0;
    };

    std::array<Region, kMaximumRegions> regions_{};
    std::size_t regionCount_ = 0;

    static constexpr bool validRange(std::uint32_t base,
                                     std::uint32_t count) noexcept {
        return count > 0u &&
            static_cast<std::uint64_t>(base) + count <=
                kAddressSpaceWordCount;
    }

    static bool regionIsReadable(const Region& region) noexcept {
        return (region.readOnlyView != nullptr &&
                region.readOnlyView->isAttached()) ||
               (region.writableBytes != nullptr &&
                static_cast<std::uint64_t>(region.wordCount) * 2u <=
                    region.writableByteCount);
    }

    bool addRegion(const Region& candidate) noexcept {
        if (regionCount_ >= regions_.size())
            return false;

        const auto candidateStart =
            static_cast<std::uint64_t>(candidate.baseWordAddress);
        const auto candidateEnd = candidateStart + candidate.wordCount;
        for (std::size_t i = 0; i < regionCount_; ++i) {
            const auto& existing = regions_[i];
            const auto existingStart =
                static_cast<std::uint64_t>(existing.baseWordAddress);
            const auto existingEnd = existingStart + existing.wordCount;
            if (candidateStart < existingEnd && existingStart < candidateEnd)
                return false;
        }

        regions_[regionCount_++] = candidate;
        return true;
    }

    const Region* findRegion(std::uint32_t wordAddress) const noexcept {
        for (std::size_t i = 0; i < regionCount_; ++i) {
            const auto& region = regions_[i];
            if (wordAddress >= region.baseWordAddress &&
                static_cast<std::uint64_t>(wordAddress - region.baseWordAddress) <
                    region.wordCount)
                return &region;
        }
        return nullptr;
    }

    Region* findRegion(std::uint32_t wordAddress) noexcept {
        for (std::size_t i = 0; i < regionCount_; ++i) {
            auto& region = regions_[i];
            if (wordAddress >= region.baseWordAddress &&
                static_cast<std::uint64_t>(wordAddress - region.baseWordAddress) <
                    region.wordCount)
                return &region;
        }
        return nullptr;
    }
};

} // namespace technox
