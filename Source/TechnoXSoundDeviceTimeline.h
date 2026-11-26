#pragma once

// Offline, diagnostic-only merger for IC4's external sound-device activity.
// This helper is intentionally not used by the real-time audio callback.
// Equal-cycle entries are kept stable, but their cross-trace sub-order is not
// claimed because the underlying trace rings do not share a global event ID.

#include "TechnoX8032Machine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace technox {

enum class SoundDeviceTimelineKind : std::uint8_t {
    ic51Read,
    soundHostRead,
    soundHostWrite,
    ff10Read,
    ff10Write,
    soundMemoryRead,
    soundMemoryWrite,
    p3Write,
    p3Bit2ControlWrite,
    soundBusModeWrite
};

struct SoundDeviceTimelineEntry {
    std::uint64_t machineCycle = 0;
    SoundDeviceTimelineKind kind = SoundDeviceTimelineKind::soundHostWrite;
    std::uint16_t instructionPC = 0;
    std::uint16_t callerPC = 0;
    std::uint16_t address = 0;
    std::uint8_t value = 0xff;
    std::uint8_t previousValue = 0xff;
    std::uint8_t port2 = 0xff;
    std::uint8_t port3 = 0xff;
    std::uint8_t busMode = 0;
    std::uint8_t bank = 0xff;
    std::uint8_t index = 0xff;
    bool ic51Selected = false;
    bool write = false;
    bool bit2Changed = false;
};

class SoundDeviceTimeline {
public:
    // Collect the most recent retained events from all relevant trace rings.
    // This allocates memory and is for tests/research reporting only.
    static std::vector<SoundDeviceTimelineEntry> collect(
        const TechnoX8032Machine& machine,
        std::size_t maximumEntries = 4096u) {
        std::vector<SoundDeviceTimelineEntry> entries;
        entries.reserve(estimateRetained(machine));

        appendRecent(entries, machine.ic51ReadTraceCount(), 16384u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::IC51ReadEvent& {
                return machine.ic51ReadTraceAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::ic51Read;
                out.instructionPC = e.instructionPC;
                out.callerPC = e.returnPC;
                out.address = e.address;
                out.value = e.value;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.busMode = e.soundBusMode;
                out.bank = e.bank;
                out.ic51Selected = e.ic51Selected;
                return out;
            });

        appendRecent(entries, machine.soundHostReadCount(), 2048u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::soundHostReadEvent& {
                return machine.soundHostReadAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::soundHostRead;
                out.instructionPC = e.instructionPC;
                out.callerPC = e.returnPC;
                out.address = static_cast<std::uint16_t>(0x8000u + e.registerIndex);
                out.value = e.value;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.busMode = e.soundBusMode;
                out.index = e.deviceIndex;
                out.ic51Selected = e.ic51Selected;
                return out;
            });

        appendRecent(entries, machine.soundHostWriteCount(), 4096u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::soundHostWriteEvent& {
                return machine.soundHostWriteAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::soundHostWrite;
                out.instructionPC = e.instructionPC;
                out.callerPC = e.returnPC;
                out.address = static_cast<std::uint16_t>(0x8000u + e.registerIndex);
                out.value = e.value;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.busMode = e.soundBusMode;
                out.index = e.deviceIndex;
                out.ic51Selected = e.ic51Selected;
                out.write = true;
                return out;
            });

        appendRecent(entries, machine.ff10ReadCount(), 4096u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::FF10ReadEvent& {
                return machine.ff10ReadAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::ff10Read;
                out.instructionPC = e.instructionPC;
                out.callerPC = e.stackReturnPC;
                out.address = e.address;
                out.value = e.value;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.busMode = e.soundBusMode;
                out.ic51Selected = e.ic51Selected;
                return out;
            });

        appendRecent(entries, machine.ff10WriteCount(), 4096u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::FF10WriteEvent& {
                return machine.ff10WriteAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::ff10Write;
                out.instructionPC = e.instructionPC;
                out.callerPC = e.stackReturnPC;
                out.address = e.address;
                out.value = e.value;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.busMode = e.soundBusMode;
                out.ic51Selected = e.ic51Selected;
                out.write = true;
                return out;
            });

        appendRecent(entries, machine.soundMemoryReadTraceCount(), 8192u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::SoundMemoryReadEvent& {
                return machine.soundMemoryReadTraceAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::soundMemoryRead;
                out.instructionPC = e.instructionPC;
                out.callerPC = e.returnPC;
                out.address = e.address;
                out.value = e.value;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.busMode = e.mode;
                out.ic51Selected = e.ic51Selected;
                return out;
            });

        appendRecent(entries, machine.soundMemoryWriteTraceCount(), 8192u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::SoundMemoryWriteEvent& {
                return machine.soundMemoryWriteTraceAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::soundMemoryWrite;
                out.instructionPC = e.instructionPC;
                out.callerPC = e.returnPC;
                out.address = e.address;
                out.value = e.value;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.busMode = e.mode;
                out.ic51Selected = e.ic51Selected;
                out.write = true;
                return out;
            });

        appendRecent(entries, machine.portWriteCount(), 4096u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::PortWriteEvent& {
                return machine.portWriteAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::p3Write;
                out.instructionPC = e.instructionPC;
                out.address = e.port;
                out.value = e.value;
                out.previousValue = e.previousValue;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.busMode = e.soundBusMode;
                out.index = e.port;
                out.write = true;
                return out;
            },
            [](const auto& e) { return e.port == 3u; });

        appendRecent(entries, machine.p3Bit2ControlWriteCount(), 128u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::P3Bit2ControlWriteEvent& {
                return machine.p3Bit2ControlWriteAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::p3Bit2ControlWrite;
                out.instructionPC = e.instructionPC;
                out.callerPC = e.returnPC;
                out.address = 3u;
                out.value = e.port3;
                out.previousValue = e.previousPort3;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.busMode = e.soundBusMode;
                out.bit2Changed = e.bit2Changed;
                out.write = true;
                return out;
            });

        appendRecent(entries, machine.soundBusModeWriteTraceCount(), 512u,
            [&machine](std::size_t i) -> const TechnoX8032Machine::SoundBusModeWriteEvent& {
                return machine.soundBusModeWriteTraceAt(i);
            },
            [](const auto& e) {
                SoundDeviceTimelineEntry out{};
                out.machineCycle = e.machineCycle;
                out.kind = SoundDeviceTimelineKind::soundBusModeWrite;
                out.instructionPC = e.instructionPC;
                out.callerPC = e.returnPC;
                out.address = 0x008eu;
                out.value = e.value;
                out.previousValue = e.previousValue;
                out.port2 = e.port2;
                out.port3 = e.port3;
                out.ic51Selected = e.ic51Selected;
                out.write = true;
                return out;
            });

        std::stable_sort(entries.begin(), entries.end(),
            [](const SoundDeviceTimelineEntry& a, const SoundDeviceTimelineEntry& b) {
                return a.machineCycle < b.machineCycle;
            });

        if (maximumEntries != 0u && entries.size() > maximumEntries)
            entries.erase(entries.begin(), entries.end() -
                static_cast<std::ptrdiff_t>(maximumEntries));
        return entries;
    }

    static const char* kindName(SoundDeviceTimelineKind kind) noexcept {
        switch (kind) {
            case SoundDeviceTimelineKind::ic51Read: return "IC51-R";
            case SoundDeviceTimelineKind::soundHostRead: return "HOST-R";
            case SoundDeviceTimelineKind::soundHostWrite: return "HOST-W";
            case SoundDeviceTimelineKind::ff10Read: return "FF10-R";
            case SoundDeviceTimelineKind::ff10Write: return "FF10-W";
            case SoundDeviceTimelineKind::soundMemoryRead: return "SMEM-R";
            case SoundDeviceTimelineKind::soundMemoryWrite: return "SMEM-W";
            case SoundDeviceTimelineKind::p3Write: return "P3-W";
            case SoundDeviceTimelineKind::p3Bit2ControlWrite: return "P3.2-W";
            case SoundDeviceTimelineKind::soundBusModeWrite: return "SFR8E-W";
        }
        return "UNKNOWN";
    }

private:
    template <typename Getter, typename Converter, typename Predicate>
    static void appendRecent(std::vector<SoundDeviceTimelineEntry>& out,
                             std::size_t total, std::size_t capacity,
                             Getter get, Converter convert, Predicate include) {
        const auto retained = std::min(total, capacity);
        const auto first = total - retained;
        for (std::size_t i = first; i < total; ++i) {
            const auto& event = get(i);
            if (include(event)) out.push_back(convert(event));
        }
    }

    template <typename Getter, typename Converter>
    static void appendRecent(std::vector<SoundDeviceTimelineEntry>& out,
                             std::size_t total, std::size_t capacity,
                             Getter get, Converter convert) {
        appendRecent(out, total, capacity, get, convert,
            [](const auto&) { return true; });
    }

    static std::size_t estimateRetained(const TechnoX8032Machine& m) noexcept {
        return std::min<std::size_t>(m.ic51ReadTraceCount(), 16384u) +
            std::min<std::size_t>(m.soundHostReadCount(), 2048u) +
            std::min<std::size_t>(m.soundHostWriteCount(), 4096u) +
            std::min<std::size_t>(m.ff10ReadCount(), 4096u) +
            std::min<std::size_t>(m.ff10WriteCount(), 4096u) +
            std::min<std::size_t>(m.soundMemoryReadTraceCount(), 8192u) +
            std::min<std::size_t>(m.soundMemoryWriteTraceCount(), 8192u) +
            std::min<std::size_t>(m.portWriteCount(), 4096u) +
            std::min<std::size_t>(m.p3Bit2ControlWriteCount(), 128u) +
            std::min<std::size_t>(m.soundBusModeWriteTraceCount(), 512u);
    }
};

} // namespace technox
