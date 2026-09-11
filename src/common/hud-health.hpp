// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace chatview {

enum class HudProvider : std::uint8_t {
    Unknown = 0U,
    Weflab = 1U,
    Chzzk = 2U,
    Soop = 3U,
    YouTube = 4U,
};

enum class HudPageState : std::uint8_t {
    Unknown = 0U,
    Starting = 1U,
    SetupRequired = 2U,
    Loading = 3U,
    Ready = 4U,
    Retrying = 5U,
    Recovering = 6U,
    LoginRequired = 7U,
    Offline = 8U,
    LayoutChanged = 9U,
    Fatal = 10U,
};

struct HudHealthSnapshot {
    HudPageState state = HudPageState::Unknown;
    HudProvider provider = HudProvider::Unknown;
    std::uint16_t detail_code = 0U;
};

[[nodiscard]] constexpr bool is_valid_hud_provider(
    HudProvider provider) noexcept
{
    return static_cast<std::uint8_t>(provider) <=
           static_cast<std::uint8_t>(HudProvider::YouTube);
}

[[nodiscard]] constexpr bool is_valid_hud_page_state(
    HudPageState state) noexcept
{
    return static_cast<std::uint8_t>(state) <=
           static_cast<std::uint8_t>(HudPageState::Fatal);
}

[[nodiscard]] constexpr bool is_valid_hud_health(
    const HudHealthSnapshot &snapshot) noexcept
{
    return is_valid_hud_provider(snapshot.provider) &&
           is_valid_hud_page_state(snapshot.state);
}

[[nodiscard]] constexpr std::uint32_t encode_hud_health(
    const HudHealthSnapshot &snapshot) noexcept
{
    return static_cast<std::uint32_t>(snapshot.state) |
           (static_cast<std::uint32_t>(snapshot.provider) << 8U) |
           (static_cast<std::uint32_t>(snapshot.detail_code) << 16U);
}

[[nodiscard]] constexpr HudHealthSnapshot decode_hud_health(
    std::uint32_t value) noexcept
{
    return HudHealthSnapshot{
        static_cast<HudPageState>(value & 0xffU),
        static_cast<HudProvider>((value >> 8U) & 0xffU),
        static_cast<std::uint16_t>((value >> 16U) & 0xffffU)};
}

static_assert(is_valid_hud_health(HudHealthSnapshot{}));
static_assert(
    decode_hud_health(encode_hud_health(HudHealthSnapshot{
        HudPageState::Ready,
        HudProvider::YouTube,
        0x1234U}))
        .detail_code == 0x1234U);

} // namespace chatview
