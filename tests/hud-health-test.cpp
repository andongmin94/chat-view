// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/hud-health.hpp"

#include <cstdint>
#include <iostream>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    using chatview::HudHealthSnapshot;
    using chatview::HudPageState;
    using chatview::HudProvider;

    constexpr HudHealthSnapshot expected{
        HudPageState::Retrying,
        HudProvider::Chzzk,
        0xbeefU};
    constexpr std::uint32_t encoded =
        chatview::encode_hud_health(expected);
    constexpr HudHealthSnapshot decoded =
        chatview::decode_hud_health(encoded);

    if (decoded.state != expected.state ||
        decoded.provider != expected.provider ||
        decoded.detail_code != expected.detail_code) {
        return fail("HUD health did not round trip");
    }

    constexpr HudHealthSnapshot no_detail{
        HudPageState::Ready,
        HudProvider::YouTube,
        0U};
    if (!chatview::is_valid_hud_health(no_detail)) {
        return fail("A valid HUD health snapshot was rejected");
    }

    constexpr HudHealthSnapshot connection_lost{
        HudPageState::ConnectionLost,
        HudProvider::Soop,
        7U};
    if (!chatview::is_valid_hud_health(connection_lost)) {
        return fail("A valid connection-loss state was rejected");
    }

    constexpr HudHealthSnapshot system_paused{
        HudPageState::SystemPaused,
        HudProvider::YouTube,
        0x7301U};
    constexpr HudHealthSnapshot system_resuming{
        HudPageState::SystemResuming,
        HudProvider::YouTube,
        0x7302U};
    if (!chatview::is_valid_hud_health(system_paused) ||
        !chatview::is_valid_hud_health(system_resuming)) {
        return fail("A valid Windows lifecycle state was rejected");
    }

    constexpr HudHealthSnapshot invalid_state{
        static_cast<HudPageState>(0xffU),
        HudProvider::Unknown,
        0U};
    if (chatview::is_valid_hud_health(invalid_state)) {
        return fail("An unknown HUD page state was accepted");
    }

    constexpr HudHealthSnapshot invalid_provider{
        HudPageState::Ready,
        static_cast<HudProvider>(0xffU),
        0U};
    if (chatview::is_valid_hud_health(invalid_provider)) {
        return fail("An unknown HUD provider was accepted");
    }

    constexpr std::uint32_t reserved_bits = 0xa55a0000U;
    constexpr HudHealthSnapshot detail_snapshot =
        chatview::decode_hud_health(
            chatview::encode_hud_health(no_detail) | reserved_bits);
    if (detail_snapshot.detail_code != 0xa55aU ||
        detail_snapshot.state != HudPageState::Ready ||
        detail_snapshot.provider != HudProvider::YouTube) {
        return fail("HUD health fields overlapped in the packed representation");
    }

    return 0;
}
