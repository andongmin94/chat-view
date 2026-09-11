// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kRuntimeExitCodeUnavailable = 0xffffffffU;
inline constexpr std::uint32_t kMaximumRuntimeFailureCount = 6U;

enum class RuntimeRestartReason : std::uint32_t {
    None = 0U,
    LaunchFailure = 1U,
    StartupTimeout = 2U,
    WebViewFailure = 3U,
    NavigationFailure = 4U,
    CaptureExclusionFailure = 5U,
    ReadySignalFailure = 6U,
    PlacementFailure = 7U,
    PageHealthTimeout = 8U,
    ConnectionRecovery = 9U,
    SystemLifecycleRecovery = 10U,
    UnexpectedExit = 11U,
    ManualRestart = 12U,
};

enum RuntimeTelemetryFlag : std::uint32_t {
    RuntimeTelemetryNone = 0U,
    RuntimeTelemetryHistoryValid = 1U << 0U,
    RuntimeTelemetryAutomatic = 1U << 1U,
    RuntimeTelemetryCircuitOpen = 1U << 2U,
};

inline constexpr std::uint32_t kKnownRuntimeTelemetryFlags =
    RuntimeTelemetryHistoryValid | RuntimeTelemetryAutomatic |
    RuntimeTelemetryCircuitOpen;

struct RuntimeTelemetrySnapshot {
    std::uint32_t last_exit_code = kRuntimeExitCodeUnavailable;
    RuntimeRestartReason restart_reason = RuntimeRestartReason::None;
    std::uint32_t consecutive_failures = 0U;
    std::uint32_t flags = RuntimeTelemetryNone;
    std::uint64_t event_filetime_utc = 0U;

    bool operator==(const RuntimeTelemetrySnapshot &) const = default;
};

[[nodiscard]] constexpr bool has_runtime_telemetry_flag(
    const RuntimeTelemetrySnapshot &snapshot,
    RuntimeTelemetryFlag flag) noexcept
{
    return (snapshot.flags & static_cast<std::uint32_t>(flag)) != 0U;
}

[[nodiscard]] constexpr bool is_valid_runtime_restart_reason(
    RuntimeRestartReason reason) noexcept
{
    return static_cast<std::uint32_t>(reason) <=
           static_cast<std::uint32_t>(RuntimeRestartReason::ManualRestart);
}

[[nodiscard]] constexpr bool is_valid_runtime_telemetry(
    const RuntimeTelemetrySnapshot &snapshot) noexcept
{
    if ((snapshot.flags & ~kKnownRuntimeTelemetryFlags) != 0U ||
        !is_valid_runtime_restart_reason(snapshot.restart_reason) ||
        snapshot.consecutive_failures > kMaximumRuntimeFailureCount) {
        return false;
    }

    const bool history_valid = has_runtime_telemetry_flag(
        snapshot, RuntimeTelemetryHistoryValid);
    const bool automatic = has_runtime_telemetry_flag(
        snapshot, RuntimeTelemetryAutomatic);
    const bool circuit_open = has_runtime_telemetry_flag(
        snapshot, RuntimeTelemetryCircuitOpen);

    if (!history_valid) {
        return snapshot.flags == RuntimeTelemetryNone &&
               snapshot.last_exit_code == kRuntimeExitCodeUnavailable &&
               snapshot.restart_reason == RuntimeRestartReason::None &&
               snapshot.consecutive_failures == 0U &&
               snapshot.event_filetime_utc == 0U;
    }

    if (snapshot.restart_reason == RuntimeRestartReason::None ||
        snapshot.event_filetime_utc == 0U ||
        circuit_open !=
            (snapshot.consecutive_failures == kMaximumRuntimeFailureCount)) {
        return false;
    }

    if (snapshot.restart_reason == RuntimeRestartReason::ManualRestart) {
        return !automatic && !circuit_open &&
               snapshot.consecutive_failures == 0U &&
               snapshot.last_exit_code == kRuntimeExitCodeUnavailable;
    }

    return automatic &&
           (snapshot.consecutive_failures == 0U || automatic);
}

[[nodiscard]] constexpr RuntimeRestartReason
runtime_restart_reason_from_exit_code(std::uint32_t exit_code) noexcept
{
    switch (exit_code) {
    case 2U:
        return RuntimeRestartReason::WebViewFailure;
    case 10U:
        return RuntimeRestartReason::NavigationFailure;
    case 11U:
        return RuntimeRestartReason::CaptureExclusionFailure;
    case 13U:
        return RuntimeRestartReason::ReadySignalFailure;
    case 18U:
        return RuntimeRestartReason::PlacementFailure;
    case 19U:
        return RuntimeRestartReason::PageHealthTimeout;
    case 20U:
        return RuntimeRestartReason::ConnectionRecovery;
    case 21U:
        return RuntimeRestartReason::SystemLifecycleRecovery;
    default:
        return RuntimeRestartReason::UnexpectedExit;
    }
}

static_assert(is_valid_runtime_telemetry(RuntimeTelemetrySnapshot{}));
static_assert(
    runtime_restart_reason_from_exit_code(19U) ==
    RuntimeRestartReason::PageHealthTimeout);

} // namespace chatview
