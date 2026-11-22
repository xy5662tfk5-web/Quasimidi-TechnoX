#pragma once

// Machine-cycle accurate timer/counter register model for Intel 8032 (8052).
// Pin-driven counter edges are sampled at machine-cycle boundaries.

#include "MCS51Core.h"

#include <cstdint>

namespace technox {

class MCS51TimerUnit {
public:
    void reset() noexcept {
        previousP3_ = 0xff;
        previousP1_ = 0xff;
    }

    void advanceMachineCycles(MCS51Bus& bus, unsigned cycles) noexcept {
        for (unsigned i = 0; i < cycles; ++i) {
            const auto p3 = bus.readInternal(0xb0u);
            const auto p1 = bus.readInternal(0x90u);
            const bool t0Falling = (previousP3_ & 0x10u) != 0 && (p3 & 0x10u) == 0;
            const bool t1Falling = (previousP3_ & 0x20u) != 0 && (p3 & 0x20u) == 0;
            const bool int0High = (p3 & 0x04u) != 0;
            const bool int1High = (p3 & 0x08u) != 0;
            const bool t2Falling = (previousP1_ & 0x01u) != 0 && (p1 & 0x01u) == 0;
            const bool t2exFalling = (previousP1_ & 0x02u) != 0 && (p1 & 0x02u) == 0;

            advanceTimer0And1(bus, t0Falling, t1Falling, int0High, int1High);
            advanceTimer2(bus, t2Falling, t2exFalling);

            previousP3_ = p3;
            previousP1_ = p1;
        }
    }

private:
    static void setFlag(MCS51Bus& bus, std::uint8_t address,
                        std::uint8_t mask) noexcept {
        bus.writeInternal(address,
            static_cast<std::uint8_t>(bus.readInternal(address) | mask));
    }

    static void incrementTimer(MCS51Bus& bus, std::uint8_t highAddress,
                               std::uint8_t lowAddress, std::uint8_t tconMask,
                               std::uint8_t mode) noexcept {
        auto low = bus.readInternal(lowAddress);
        auto high = bus.readInternal(highAddress);
        bool overflow = false;

        if (mode == 0u) { // 13-bit mode
            auto count = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(high) << 5u) | (low & 0x1fu));
            count = static_cast<std::uint16_t>((count + 1u) & 0x1fffu);
            overflow = count == 0;
            high = static_cast<std::uint8_t>(count >> 5u);
            low = static_cast<std::uint8_t>((low & 0xe0u) | (count & 0x1fu));
        } else if (mode == 1u) { // 16-bit mode
            auto count = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(high) << 8u) | low);
            count = static_cast<std::uint16_t>(count + 1u);
            overflow = count == 0;
            high = static_cast<std::uint8_t>(count >> 8u);
            low = static_cast<std::uint8_t>(count);
        } else { // mode 2: 8-bit auto-reload
            low = static_cast<std::uint8_t>(low + 1u);
            overflow = low == 0;
            if (overflow)
                low = bus.readInternal(highAddress);
        }

        bus.writeInternal(lowAddress, low);
        if (mode != 2u)
            bus.writeInternal(highAddress, high);
        if (overflow)
            setFlag(bus, 0x88u, tconMask);
    }

    static void advanceTimer0And1(MCS51Bus& bus, bool t0Falling,
                                  bool t1Falling, bool int0High,
                                  bool int1High) noexcept {
        const auto tmod = bus.readInternal(0x89u);
        auto tcon = bus.readInternal(0x88u);
        const auto mode0 = static_cast<std::uint8_t>(tmod & 0x03u);
        const auto mode1 = static_cast<std::uint8_t>((tmod >> 4u) & 0x03u);

        if (mode0 == 3u) {
            // Split mode: TL0 is Timer 0; TH0 is an 8-bit timer controlled by TR1/TF1.
            const bool runTL0 = (tcon & 0x10u) != 0 &&
                ((tmod & 0x08u) == 0 || int0High) &&
                ((tmod & 0x04u) == 0 || t0Falling);
            if (runTL0) {
                auto value = static_cast<std::uint8_t>(bus.readInternal(0x8au) + 1u);
                bus.writeInternal(0x8au, value);
                if (value == 0) setFlag(bus, 0x88u, 0x20u);
            }
            if ((tcon & 0x40u) != 0) {
                auto value = static_cast<std::uint8_t>(bus.readInternal(0x8cu) + 1u);
                bus.writeInternal(0x8cu, value);
                if (value == 0) setFlag(bus, 0x88u, 0x80u);
            }
            return; // Timer 1 itself is stopped in split mode.
        }

        const bool run0 = (tcon & 0x10u) != 0 &&
            ((tmod & 0x08u) == 0 || int0High) &&
            ((tmod & 0x04u) == 0 || t0Falling);
        if (run0)
            incrementTimer(bus, 0x8cu, 0x8au, 0x20u, mode0);

        const bool run1 = (tcon & 0x40u) != 0 &&
            ((tmod & 0x80u) == 0 || int1High) &&
            ((tmod & 0x40u) == 0 || t1Falling);
        if (run1 && mode1 != 3u)
            incrementTimer(bus, 0x8du, 0x8bu, 0x80u, mode1);
    }

    static void advanceTimer2(MCS51Bus& bus, bool t2Falling,
                              bool t2exFalling) noexcept {
        auto t2con = bus.readInternal(0xc8u);
        const bool running = (t2con & 0x04u) != 0;
        const bool counterMode = (t2con & 0x02u) != 0;
        const bool captureMode = (t2con & 0x01u) != 0;
        const bool baudMode = (t2con & 0x30u) != 0;
        const bool externalEnable = (t2con & 0x08u) != 0;

        if (running && (!counterMode || t2Falling)) {
            auto count = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(bus.readInternal(0xcdu)) << 8u) |
                bus.readInternal(0xccu));
            count = static_cast<std::uint16_t>(count + 1u);
            const bool overflow = count == 0;
            if (overflow) {
                if (!captureMode || baudMode) {
                    count = static_cast<std::uint16_t>(
                        (static_cast<std::uint16_t>(bus.readInternal(0xcbu)) << 8u) |
                        bus.readInternal(0xcau));
                }
                if (!baudMode)
                    t2con = static_cast<std::uint8_t>(t2con | 0x80u); // TF2
            }
            bus.writeInternal(0xcdu, static_cast<std::uint8_t>(count >> 8u));
            bus.writeInternal(0xccu, static_cast<std::uint8_t>(count));
        }

        if (externalEnable && t2exFalling) {
            if (captureMode) {
                bus.writeInternal(0xcbu, bus.readInternal(0xcdu));
                bus.writeInternal(0xcau, bus.readInternal(0xccu));
            } else if (running) {
                bus.writeInternal(0xcdu, bus.readInternal(0xcbu));
                bus.writeInternal(0xccu, bus.readInternal(0xcau));
            }
            t2con = static_cast<std::uint8_t>(t2con | 0x40u); // EXF2
        }
        bus.writeInternal(0xc8u, t2con);
    }

    std::uint8_t previousP3_ = 0xff;
    std::uint8_t previousP1_ = 0xff;
};

} // namespace technox
