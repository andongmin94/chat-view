// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace chatview {

inline constexpr std::uint32_t kSharedStateMagic = 0x43485657U; // "CHVW"
inline constexpr std::uint32_t kSharedStateVersion = 2U;
inline constexpr std::size_t kSceneNameCapacity = 192U;

enum SharedStateFlag : std::uint32_t {
    SharedStateNone = 0U,
    SharedStateStreaming = 1U << 0U,
    SharedStateRecording = 1U << 1U,
    SharedStateShutdown = 1U << 31U,
};

struct SharedState {
    std::uint32_t magic;
    std::uint32_t version;
    volatile LONG sequence;
    std::uint32_t flags;
    std::uint64_t generation;
    char scene_name[kSceneNameCapacity]{};
    std::uint8_t reserved[8]{};
};

struct SharedSnapshot {
    std::uint32_t flags = SharedStateNone;
    std::uint64_t generation = 0U;
    std::array<char, kSceneNameCapacity> scene_name{};
};

[[nodiscard]] inline bool has_flag(const SharedSnapshot &snapshot, SharedStateFlag flag) noexcept
{
    return (snapshot.flags & static_cast<std::uint32_t>(flag)) != 0U;
}

[[nodiscard]] inline std::string_view scene_name_view(const SharedSnapshot &snapshot) noexcept
{
    const auto terminator = std::find(snapshot.scene_name.begin(), snapshot.scene_name.end(), '\0');
    return std::string_view(snapshot.scene_name.data(),
                            static_cast<std::size_t>(terminator - snapshot.scene_name.begin()));
}

static_assert(sizeof(LONG) == sizeof(std::int32_t));
static_assert(sizeof(SharedState) == 224U);

} // namespace chatview
