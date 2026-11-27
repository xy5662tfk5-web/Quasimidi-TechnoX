#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace technox {

enum class ModSource : std::uint8_t { modWheel, velocity, keyTrack, aftertouch, pitchBend, freeController };
enum class ModDestination : std::uint8_t { pitch, cutoff, level, panorama, lfo, fx2Control, arpGate };

struct ModRoute {
    ModSource source = ModSource::modWheel;
    ModDestination destination = ModDestination::cutoff;
    float amount = 0.0f;
    bool enabled = false;
};

// Generic modulation routing infrastructure. It starts with no routes:
// hardware defaults must be established from captures/manual evidence.
class ModulationMatrix {
public:
    static constexpr std::size_t kMaximumRoutes = 16;

    bool setRoute(std::size_t index, ModSource source, ModDestination destination, float amount) noexcept {
        if (index >= routes_.size() || amount < -1.0f || amount > 1.0f)
            return false;
        routes_[index] = {source, destination, amount, true};
        return true;
    }

    void clearRoute(std::size_t index) noexcept {
        if (index < routes_.size()) routes_[index].enabled = false;
    }

    float contribution(ModDestination destination,
                       float modWheel, float velocity, float keyTrack, float aftertouch,
                       float pitchBend = 0.0f, float freeController = 0.0f) const noexcept {
        float sum = 0.0f;
        for (const auto& route : routes_) {
            if (!route.enabled || route.destination != destination) continue;
            float source = 0.0f;
            switch (route.source) {
                case ModSource::modWheel: source = modWheel; break;
                case ModSource::velocity: source = velocity; break;
                case ModSource::keyTrack: source = keyTrack; break;
                case ModSource::aftertouch: source = aftertouch; break;
                case ModSource::pitchBend: source = pitchBend; break;
                case ModSource::freeController: source = freeController; break;
            }
            sum += source * route.amount;
        }
        return sum;
    }

    const std::array<ModRoute, kMaximumRoutes>& routes() const noexcept { return routes_; }

private:
    std::array<ModRoute, kMaximumRoutes> routes_{};
};

} // namespace technox
