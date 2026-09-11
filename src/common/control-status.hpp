// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kControlStatusMagic = 0x43565354U; // "CVST"
inline constexpr std::uint32_t kControlStatusVersion = 1U;

enum ControlStatusFlag : std::uint32_t {
    ControlStatusNone = 0U,
    ControlStatusStreaming = 1U << 0U,
    ControlStatusRecording = 1U << 1U,
    ControlStatusReplayBuffer = 1U << 2U,
    ControlStatusVirtualCamera = 1U << 3U,
    ControlStatusCaptureRisk = 1U << 4U,
    ControlStatusHudRunning = 1U << 5U,
    ControlStatusHudVisible = 1U << 6U,
};

inline constexpr std::uint32_t kKnownControlStatusFlags =
    ControlStatusStreaming | ControlStatusRecording |
    ControlStatusReplayBuffer | ControlStatusVirtualCamera |
    ControlStatusCaptureRisk | ControlStatusHudRunning |
    ControlStatusHudVisible;

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
    std::uint32_t reserved32;
    std::uint64_t generation;
    std::uint64_t updated_tick_ms;
    std::uint8_t reserved[24]{};
};

struct ControlStatusSnapshot {
    std::uint32_t flags = ControlStatusNone;
    std::uint32_t hud_process_id = 0U;
    std::uint64_t generation = 0U;
    std::uint64_t updated_tick_ms = 0U;
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
    if (!are_valid_control_status_flags(snapshot.flags)) {
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
static_assert(!are_valid_control_status_flags(1U << 7U));

} // namespace chatview
