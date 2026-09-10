// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kSharedStateMagic = 0x43485657U; // "CHVW"
inline constexpr std::uint32_t kSharedStateVersion = 3U;

enum SharedStateFlag : std::uint32_t {
    SharedStateNone = 0U,
    SharedStateStreaming = 1U << 0U,
    SharedStateRecording = 1U << 1U,
    SharedStateShutdown = 1U << 31U,
};

inline constexpr std::uint32_t kKnownSharedStateFlags =
    SharedStateStreaming | SharedStateRecording | SharedStateShutdown;

[[nodiscard]] constexpr bool are_valid_shared_state_flags(
    std::uint32_t flags) noexcept
{
    return (flags & ~kKnownSharedStateFlags) == 0U;
}

struct SharedState {
    std::uint32_t magic;
    std::uint32_t version;
    volatile LONG sequence;
    std::uint32_t flags;
    std::uint64_t generation;
    std::uint8_t reserved[40]{};
};

struct SharedSnapshot {
    std::uint32_t flags = SharedStateNone;
    std::uint64_t generation = 0U;
};

[[nodiscard]] inline bool has_flag(
    const SharedSnapshot &snapshot, SharedStateFlag flag) noexcept
{
    return (snapshot.flags & static_cast<std::uint32_t>(flag)) != 0U;
}

static_assert(sizeof(LONG) == sizeof(std::int32_t));
static_assert(sizeof(SharedState) == 64U);
static_assert(are_valid_shared_state_flags(SharedStateNone));
static_assert(are_valid_shared_state_flags(kKnownSharedStateFlags));
static_assert(!are_valid_shared_state_flags(1U << 2U));

} // namespace chatview
