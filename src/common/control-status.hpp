// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/runtime-telemetry.hpp"

#include <Windows.h>

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kControlStatusMagic = 0x43565354U; // "CVST"
inline constexpr std::uint32_t kControlStatusVersion = 3U;

enum ControlStatusFlag : std::uint32_t {
    ControlStatusNone = 0U,
    ControlStatusStreaming = 1U << 0U,
    ControlStatusRecording = 1U << 1U,
    ControlStatusReplayBuffer = 1U << 2U,
    ControlStatusVirtualCamera = 1U << 3U,
    ControlStatusCaptureRisk = 1U << 4U,
    ControlStatusHudRunning = 1U << 5U,
    ControlStatusHudVisible = 1U << 6U,
    ControlStatusSceneGraphReady = 1U << 7U,
    ControlStatusDisplayCaptureActive = 1U << 8U,
};

inline constexpr std::uint32_t kKnownControlStatusFlags =
    ControlStatusStreaming | ControlStatusRecording |
    ControlStatusReplayBuffer | ControlStatusVirtualCamera |
    ControlStatusCaptureRisk | ControlStatusHudRunning |
    ControlStatusHudVisible | ControlStatusSceneGraphReady |
    ControlStatusDisplayCaptureActive;

[[nodiscard]] constexpr bool are_valid_control_status_flags(
    std::uint32_t flags) noexcept
{
    return (flags & ~kKnownControlStatusFlags) == 0U;
}

struct ControlStatus {
    std::uint32_t magic;
    std::uint32_t version;
    volatile LONG sequence;
    std::uint32_t flags;
    std::uint32_t hud_process_id;
    std::uint32_t runtime_telemetry_flags;
    std::uint64_t generation;
    std::uint64_t updated_tick_ms;
    std::uint64_t runtime_event_filetime_utc;
    std::uint32_t last_hud_exit_code;
    std::uint32_t runtime_restart_reason;
    std::uint32_t consecutive_runtime_failures;
    std::uint32_t reserved32;
};

struct ControlStatusSnapshot {
    std::uint32_t flags = ControlStatusNone;
    std::uint32_t hud_process_id = 0U;
    std::uint64_t generation = 0U;
    std::uint64_t updated_tick_ms = 0U;
    RuntimeTelemetrySnapshot runtime_telemetry;
};

[[nodiscard]] constexpr bool has_control_status_flag(
    const ControlStatusSnapshot &snapshot,
    ControlStatusFlag flag) noexcept
{
    return (snapshot.flags & static_cast<std::uint32_t>(flag)) != 0U;
}

[[nodiscard]] constexpr bool is_valid_control_status_snapshot(
    const ControlStatusSnapshot &snapshot) noexcept
{
    if (!are_valid_control_status_flags(snapshot.flags) ||
        !is_valid_runtime_telemetry(snapshot.runtime_telemetry)) {
        return false;
    }

    const bool running = has_control_status_flag(
        snapshot, ControlStatusHudRunning);
    const bool visible = has_control_status_flag(
        snapshot, ControlStatusHudVisible);
    if (visible && !running) {
        return false;
    }
    return running == (snapshot.hud_process_id != 0U);
}

static_assert(sizeof(LONG) == sizeof(std::int32_t));
static_assert(sizeof(ControlStatus) == 64U);
static_assert(are_valid_control_status_flags(ControlStatusNone));
static_assert(are_valid_control_status_flags(kKnownControlStatusFlags));
static_assert(!are_valid_control_status_flags(1U << 9U));

} // namespace chatview
