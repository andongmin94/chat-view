// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/hud-health.hpp"
#include <cstdint>

namespace chatview {
// Local UI only: no pointers, URLs, account IDs, credentials or connection
// commands. Query v2 reports display selection and local return eligibility.
// Eligibility is not proof that the server will accept the retained approval.
inline constexpr wchar_t kOpenNativeChatMessageName[] = L"ChatViewOBS.OpenNativeChat.v1";
// Local thread-only companion UI request. It only opens the existing video
// selection panel; no target selection, worker start, consent or OBS launch.
inline constexpr wchar_t kOpenCompanionVideoMessageName[] = L"ChatViewOBS.OpenCompanionVideo.v1";
inline constexpr wchar_t kQueryNativeChatMessageName[] = L"ChatViewOBS.QueryNativeChat.v2";
enum class NativeChatStatus : std::uint32_t {
    Unavailable = 0U, Idle, AwaitingApproval, Connecting, Receiving,
    Reconnecting, SigningOut, Paused, Stopping, IdleResumable,
    ExternalPage, ExternalPageResumable,
};
constexpr NativeChatStatus decode_native_chat_status(std::uint64_t value) noexcept
{
    return value <= static_cast<std::uint64_t>(NativeChatStatus::ExternalPageResumable)
        ? static_cast<NativeChatStatus>(value) : NativeChatStatus::Unavailable;
}
constexpr bool has_native_chat_flow(NativeChatStatus status) noexcept
{
    return status >= NativeChatStatus::AwaitingApproval && status <= NativeChatStatus::Stopping;
}
constexpr bool can_request_native_chat_return(NativeChatStatus status) noexcept
{
    return status == NativeChatStatus::IdleResumable || status == NativeChatStatus::ExternalPageResumable;
}
// Derived from the host's selected document and the existing private client,
// never from saved external settings or a second copy of the approval.
constexpr NativeChatStatus inactive_native_chat_status(bool worker_running,
    bool external_selected, bool resumable) noexcept
{
    if (worker_running) return NativeChatStatus::Stopping;
    if (external_selected) return resumable ? NativeChatStatus::ExternalPageResumable : NativeChatStatus::ExternalPage;
    return resumable ? NativeChatStatus::IdleResumable : NativeChatStatus::Idle;
}
// A pending native login must not inherit Ready from an old external page.
constexpr HudPageState native_chat_page_state(NativeChatStatus status) noexcept
{
    switch (status) {
    case NativeChatStatus::AwaitingApproval: return HudPageState::LoginRequired;
    case NativeChatStatus::Connecting: return HudPageState::Loading;
    case NativeChatStatus::Receiving: return HudPageState::Ready;
    case NativeChatStatus::Reconnecting:
    case NativeChatStatus::SigningOut:
    case NativeChatStatus::Stopping: return HudPageState::ConnectionLost;
    case NativeChatStatus::Paused: return HudPageState::SystemPaused;
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
    case NativeChatStatus::Stopping: return L"Stopping native delivery — return unavailable";
    case NativeChatStatus::IdleResumable: return L"Native display stopped — return can be requested";
    case NativeChatStatus::ExternalPage: return L"External page selected — no in-run return";
    case NativeChatStatus::ExternalPageResumable: return L"External page selected — return can be requested";
    default: return L"Connection status unavailable";
    }
}
constexpr const wchar_t *native_chat_status_text_ko(NativeChatStatus status) noexcept
{
    switch (status) {
    case NativeChatStatus::Idle: return L"자체 채팅 미연결 · 연결창에서 로그인하세요.";
    case NativeChatStatus::AwaitingApproval: return L"브라우저 승인 대기 · 아직 자체 채팅을 표시하지 않습니다.";
    case NativeChatStatus::Connecting: return L"자체 채팅 연결 중 · 새 화면을 준비하고 있습니다.";
    case NativeChatStatus::Receiving: return L"자체 채팅 표시 중 · 승인된 연결에서 수신합니다.";
    case NativeChatStatus::Reconnecting: return L"자체 채팅 재연결 중 · 이전 채팅은 지웠습니다.";
    case NativeChatStatus::SigningOut: return L"로그아웃 중 · 서버 확인 전이며 복귀할 수 없습니다.";
    case NativeChatStatus::Paused: return L"로컬 보호 중 · 자체 채팅 복귀가 차단됐습니다.";
    case NativeChatStatus::Stopping: return L"자체 채팅 종료 처리 중 · 복귀는 아직 불가능합니다.";
    case NativeChatStatus::IdleResumable: return L"자체 채팅 표시 중지 · 현재 승인으로 복귀 요청 가능";
    case NativeChatStatus::ExternalPage: return L"외부 페이지 선택 · 복귀 가능한 현재 승인이 없습니다.";
    case NativeChatStatus::ExternalPageResumable: return L"외부 페이지 선택 · 현재 승인으로 복귀 요청 가능";
    default: return L"현재 표시·복귀 상태를 확인하지 못했습니다.";
    }
}
} // namespace chatview
