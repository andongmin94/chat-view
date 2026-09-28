// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <limits>

namespace chatview {
inline constexpr std::uint64_t kObsOutputSampleMaxAgeMs = 3000U;
inline constexpr std::uint64_t kOutputReportLifetimeMs = 15000U;
struct ObsOutputObservation {
    std::uint64_t sampled_tick = 0U;
    bool streaming = false;
    bool recording = false;
};
// A single lock-free IPC word binds both flags to the actual frontend sample.
// Zero means unobserved. Uptime is local monotonic time, never a server clock.
constexpr std::uint64_t pack_obs_output(ObsOutputObservation value) noexcept
{
    return !value.sampled_tick || value.sampled_tick > (std::numeric_limits<std::uint64_t>::max() >> 2U)
        ? 0U : (value.sampled_tick << 2U) | (value.streaming ? 1U : 0U) | (value.recording ? 2U : 0U);
}
constexpr ObsOutputObservation unpack_obs_output(std::uint64_t value) noexcept
{
    return {value >> 2U, (value & 1U) != 0U, (value & 2U) != 0U};
}
constexpr bool fresh_obs_output(ObsOutputObservation value, std::uint64_t now) noexcept
{
    return value.sampled_tick != 0U && value.sampled_tick <= now &&
        now - value.sampled_tick < kObsOutputSampleMaxAgeMs;
}
struct ReportedOutput {
    std::uint64_t expires_tick = 0U;
    bool streaming = false;
    bool recording = false;
    bool operator==(const ReportedOutput &) const = default;
};
inline bool expire_reported_output(ReportedOutput &value, std::uint64_t now) noexcept
{
    if (!value.expires_tick || now < value.expires_tick) return false;
    value = {}; return true;
}
} // namespace chatview
