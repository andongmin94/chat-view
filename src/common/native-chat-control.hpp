// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/hud-health.hpp"
#include <cstdint>

namespace chatview {
// Local UI commands only. Neither message carries pointers, URLs, account IDs,
// credentials, role grants or a request to start/stop a connection.
inline constexpr wchar_t kOpenNativeChatMessageName[] = L"ChatViewOBS.OpenNativeChat.v1";
inline constexpr wchar_t kQueryNativeChatMessageName[] = L"ChatViewOBS.QueryNativeChat.v1";
enum class NativeChatStatus : std::uint32_t {
    Unavailable = 0U, Idle, AwaitingApproval, Connecting, Receiving,
    Reconnecting, SigningOut, Paused,
};
constexpr NativeChatStatus decode_native_chat_status(std::uint64_t value) noexcept
{
    return value <= static_cast<std::uint64_t>(NativeChatStatus::Paused)
        ? static_cast<NativeChatStatus>(value) : NativeChatStatus::Unavailable;
}
constexpr bool has_native_chat_flow(NativeChatStatus status) noexcept
{
    return status >= NativeChatStatus::AwaitingApproval && status <= NativeChatStatus::Paused;
}
// A pending native login must not inherit Ready from an old external page.
constexpr HudPageState native_chat_page_state(NativeChatStatus status) noexcept
{
    switch (status) {
    case NativeChatStatus::AwaitingApproval: return HudPageState::LoginRequired;
    case NativeChatStatus::Connecting: return HudPageState::Loading;
    case NativeChatStatus::Receiving: return HudPageState::Ready;
    case NativeChatStatus::Reconnecting:
    case NativeChatStatus::SigningOut: return HudPageState::ConnectionLost;
    case NativeChatStatus::Paused: return HudPageState::SystemPaused;
    case NativeChatStatus::Unavailable:
    case NativeChatStatus::Idle:
    default: return HudPageState::Unknown;
    }
}
constexpr const wchar_t *native_chat_status_text(NativeChatStatus status) noexcept
{
    switch (status) {
    case NativeChatStatus::Idle: return L"Not connected — open the connection panel";
    case NativeChatStatus::AwaitingApproval: return L"Waiting for browser approval";
    case NativeChatStatus::Connecting: return L"Connecting — chat display is not ready yet";
    case NativeChatStatus::Receiving: return L"Approved connection — chat surface is receiving";
    case NativeChatStatus::Reconnecting: return L"Reconnecting — previous chat was cleared";
    case NativeChatStatus::SigningOut: return L"Signing out — server confirmation pending";
    case NativeChatStatus::Paused: return L"Local protection active — connection unavailable";
    case NativeChatStatus::Unavailable:
    default: return L"Connection status unavailable";
    }
}
} // namespace chatview
