#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace technox
{
constexpr std::uint8_t kManufacturerId = 0x3f;
constexpr std::uint8_t kDeviceId = 0x22;
constexpr std::uint8_t kRequestData = 0x52;
constexpr std::uint8_t kDumpData = 0x44;

struct Address
{
    std::uint8_t high = 0, middle = 0, low = 0;
};

struct Request
{
    std::uint8_t deviceChannel = 0;
    Address address;
    std::uint16_t count = 0;
};

struct Dump
{
    std::uint8_t deviceChannel = 0;
    Address address;
    std::vector<std::uint8_t> data;
};

// Parses the documented Quasimidi manufacturer-specific request packet:
// F0 3F dv 22 52 ah am al dh dl F7
inline std::optional<Request> parseRequest(const std::vector<std::uint8_t>& m)
{
    if (m.size() != 11 || m.front() != 0xf0 || m.back() != 0xf7
        || m[1] != kManufacturerId || m[3] != kDeviceId || m[4] != kRequestData)
        return std::nullopt;
    for (std::size_t i = 1; i + 1 < m.size(); ++i)
        if (m[i] & 0x80) return std::nullopt;
    Request r;
    r.deviceChannel = m[2];
    r.address = {m[5], m[6], m[7]};
    r.count = static_cast<std::uint16_t>((m[8] << 7) | m[9]);
    return r;
}

// Parses a documented dump packet. All payload bytes must be 7-bit data:
// F0 3F dv 22 44 ah am al data... F7
inline std::optional<Dump> parseDump(const std::vector<std::uint8_t>& m)
{
    if (m.size() < 9 || m.front() != 0xf0 || m.back() != 0xf7
        || m[1] != kManufacturerId || m[3] != kDeviceId || m[4] != kDumpData)
        return std::nullopt;
    for (std::size_t i = 1; i + 1 < m.size(); ++i)
        if (m[i] & 0x80) return std::nullopt;
    Dump d;
    d.deviceChannel = m[2];
    d.address = {m[5], m[6], m[7]};
    d.data.assign(m.begin() + 8, m.end() - 1);
    return d;
}

inline std::vector<std::uint8_t> makeRequest(std::uint8_t channel, Address a,
                                              std::uint16_t count)
{
    if (channel > 0x7f || a.high > 0x7f || a.middle > 0x7f || a.low > 0x7f
        || count > 0x3fff)
        return {};
    return {0xf0, kManufacturerId, channel, kDeviceId, kRequestData,
            a.high, a.middle, a.low,
            static_cast<std::uint8_t>((count >> 7) & 0x7f),
            static_cast<std::uint8_t>(count & 0x7f), 0xf7};
}

inline std::vector<std::uint8_t> makeDump(std::uint8_t channel, Address a,
                                           const std::vector<std::uint8_t>& data)
{
    if (channel > 0x7f || a.high > 0x7f || a.middle > 0x7f || a.low > 0x7f)
        return {};
    for (auto b : data) if (b > 0x7f) return {};
    std::vector<std::uint8_t> m{0xf0, kManufacturerId, channel, kDeviceId,
                                kDumpData, a.high, a.middle, a.low};
    m.insert(m.end(), data.begin(), data.end());
    m.push_back(0xf7);
    return m;
}
} // namespace technox
