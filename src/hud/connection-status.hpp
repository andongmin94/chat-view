// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud/launch-options.hpp"
#include "common/obs-output.hpp"
#include <string>
#include <string_view>

namespace chatview {
enum class DisplayRole { Gaming, Streaming };
constexpr DisplayRole display_role_for(HudLaunchMode mode) noexcept
{
    return mode == HudLaunchMode::Companion ? DisplayRole::Gaming : DisplayRole::Streaming;
}
constexpr const wchar_t *display_role_wire(DisplayRole role) noexcept
{
    return role == DisplayRole::Gaming ? L"gaming" : L"streaming";
}
constexpr const wchar_t *display_role_label(DisplayRole role) noexcept
{
    return role == DisplayRole::Gaming ? L"게임 PC · 개인 HUD" : L"송출 PC · OBS 관리 런타임";
}
inline bool connection_id(std::wstring_view value) noexcept
{
    if (value.size() != 36U) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 8U || i == 13U || i == 18U || i == 23U) {
            if (value[i] != L'-') return false;
        } else if (!((value[i] >= L'0' && value[i] <= L'9') ||
                     (value[i] >= L'a' && value[i] <= L'f'))) return false;
    }
    return true;
}
struct DisplayMembership {
    DisplayRole role = DisplayRole::Gaming;
    std::wstring broadcast_session_id;
    std::wstring connection_id;
    bool operator==(const DisplayMembership &) const = default;
};
struct DisplayConnectionState {
    DisplayMembership membership;
    unsigned gaming_connections = 0;
    unsigned streaming_connections = 0;
    ReportedOutput output{};
    bool operator==(const DisplayConnectionState &) const = default;
};
inline std::wstring connection_summary(const DisplayConnectionState &state)
{
    const auto output = state.output.expires_tick
        ? std::wstring(L"OBS 출력(송출 PC 보고): 송출 ") + (state.output.streaming ? L"활성" : L"정지") +
          L" · 녹화 출력 " + (state.output.recording ? L"활성" : L"정지")
        : std::wstring(L"OBS 출력: 확인되지 않음");
    return std::wstring(L"승인 역할: ") + display_role_label(state.membership.role) +
        L"\n공유 세션: " + state.membership.broadcast_session_id +
        L"\n서버 채팅 연결: 게임 " + std::to_wstring(state.gaming_connections) +
        L" / 송출 " + std::to_wstring(state.streaming_connections) + L" · 영상 제외 미검증\n" + output;
}
} // namespace chatview
