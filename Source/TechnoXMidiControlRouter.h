#pragma once
#include "TechnoXPartModel.h"
#include <array>
#include <cstdint>
#include <optional>

namespace technox {
struct PartParameterChange { PartParameter parameter; std::uint8_t rawValue; };

class MidiControlRouter {
public:
    MidiControlRouter() {
        nrpnMsb_.fill(0x7f); nrpnLsb_.fill(0x7f);
        rpnMsb_.fill(0x7f); rpnLsb_.fill(0x7f);
        bankSelect_.fill(0); modulationWheel_.fill(0);
    }

    std::optional<PartParameterChange> handleControlChange(int channel, int controller, int value) noexcept {
        if (!validChannel(channel) || controller < 0 || controller > 127 || value < 0 || value > 127)
            return std::nullopt;
        const auto ch = static_cast<std::size_t>(channel - 1);
        switch (controller) {
            case 0:
                if (value > 3) return std::nullopt;
                bankSelect_[ch] = static_cast<std::uint8_t>(value);
                return change(PartParameter::bankNumber, value);
            case 1:
                modulationWheel_[ch] = static_cast<std::uint8_t>(value);
                return std::nullopt;
            case 5: return change(PartParameter::portamentoTime, value);
            case 6:
                if (nrpnMsb_[ch] != 0x7f)
                    return decodePartNrpn(nrpnMsb_[ch], nrpnLsb_[ch], value);
                return std::nullopt;
            case 7: return change(PartParameter::level, value);
            case 10: return change(PartParameter::panorama, scale(value, 20));
            case 64: return change(PartParameter::holdPedal, value >= 64 ? 1 : 0);
            case 98: nrpnLsb_[ch] = static_cast<std::uint8_t>(value); return std::nullopt;
            case 99:
                nrpnMsb_[ch] = static_cast<std::uint8_t>(value);
                rpnMsb_[ch] = rpnLsb_[ch] = 0x7f;
                return std::nullopt;
            case 100: rpnLsb_[ch] = static_cast<std::uint8_t>(value); return std::nullopt;
            case 101:
                rpnMsb_[ch] = static_cast<std::uint8_t>(value);
                nrpnMsb_[ch] = nrpnLsb_[ch] = 0x7f;
                return std::nullopt;
            case 91: return change(PartParameter::fx1Send, scale(value, 63));
            case 93: return change(PartParameter::fx2Send, scale(value, 63));
            default: return std::nullopt;
        }
    }

    int bankSelectForChannel(int channel) const noexcept {
        return validChannel(channel) ? bankSelect_[static_cast<std::size_t>(channel - 1)] : 0;
    }
    int modulationWheelForChannel(int channel) const noexcept {
        return validChannel(channel) ? modulationWheel_[static_cast<std::size_t>(channel - 1)] : 0;
    }

    static std::optional<PartParameterChange> decodePartNrpn(int msb, int lsb, int value) noexcept {
        if (value < 0 || value > 127 || msb < 0 || msb > 127 || lsb < 0 || lsb > 127 || msb != 0x01)
            return std::nullopt;
        switch (lsb) {
            case 0x08: return change(PartParameter::vibratoRate, value);
            case 0x09: return change(PartParameter::vibratoDepth, value);
            case 0x10: return change(PartParameter::vibratoDelay, value);
            case 0x20: return change(PartParameter::cutoffFrequency, value);
            case 0x21: return change(PartParameter::resonance, value);
            case 0x63: return change(PartParameter::egAttack, value);
            case 0x64: return change(PartParameter::egDecay, value);
            case 0x66: return change(PartParameter::egRelease, value);
            default: return std::nullopt;
        }
    }
private:
    static bool validChannel(int channel) noexcept { return channel >= 1 && channel <= 16; }
    static std::uint8_t scale(int value, int maximum) noexcept {
        return static_cast<std::uint8_t>((value * maximum + 63) / 127);
    }
    static std::optional<PartParameterChange> change(PartParameter parameter, int value) noexcept {
        const auto index = static_cast<std::size_t>(parameter);
        if (index >= kPartParameterDescriptors.size() || value < 0 ||
            value > kPartParameterDescriptors[index].maximum) return std::nullopt;
        return PartParameterChange{parameter, static_cast<std::uint8_t>(value)};
    }
    std::array<std::uint8_t, 16> nrpnMsb_{}, nrpnLsb_{}, rpnMsb_{}, rpnLsb_{}, bankSelect_{}, modulationWheel_{};
};
} // namespace technox
