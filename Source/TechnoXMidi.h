#pragma once
#include <array>
#include <cstdint>

namespace technox
{
// Documented MIDI state model. This is protocol/state handling, not yet the
// proprietary TechnoX sound generator.
struct PartState
{
    std::uint8_t bank = 0;
    std::uint8_t program = 0;
    std::uint8_t volume = 100;
    std::uint8_t pan = 64;
    std::uint8_t modulation = 0;
    std::uint8_t sustain = 0;
    std::uint16_t pitchBend = 8192;
    std::uint8_t nrpnMsb = 0x7f, nrpnLsb = 0x7f;
    std::uint8_t rpnMsb = 0x7f, rpnLsb = 0x7f;
    std::uint8_t dataEntry = 0;
};

class MidiState
{
public:
    PartState& part(int channel) { return parts[static_cast<std::size_t>(channel & 15)]; }
    const PartState& part(int channel) const { return parts[static_cast<std::size_t>(channel & 15)]; }

    // Returns true when a documented parameter/program state changed.
    bool handle(const std::uint8_t* m, int size)
    {
        if (m == nullptr || size < 1) return false;
        const auto status = m[0];
        const auto kind = status & 0xf0;
        const int ch = status & 0x0f;
        auto& p = part(ch);
        if (kind == 0xc0 && size >= 2) { p.program = m[1] & 0x7f; return true; }
        if (kind == 0xe0 && size >= 3) {
            p.pitchBend = static_cast<std::uint16_t>((m[1] & 0x7f) | ((m[2] & 0x7f) << 7));
            return true;
        }
        if (kind != 0xb0 || size < 3) return false;
        const auto cc = m[1] & 0x7f, value = m[2] & 0x7f;
        switch (cc) {
            case 0: p.bank = value & 0x03; return true;
            case 1: p.modulation = value; return true;
            case 7: p.volume = value; return true;
            case 10: p.pan = value; return true;
            case 64: p.sustain = value >= 64 ? 1 : 0; return true;
            case 98: p.nrpnLsb = value; return true;
            case 99: p.nrpnMsb = value; return true;
            case 100: p.rpnLsb = value; return true;
            case 101: p.rpnMsb = value; return true;
            case 6: p.dataEntry = value; return true;
            default: return false;
        }
    }

private:
    std::array<PartState, 16> parts{};
};
} // namespace technox
