// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace chatview {

inline constexpr wchar_t kHudWindowClassName[] = L"ChatViewObsHudWindow";
inline constexpr wchar_t kConfigChangedMessageName[] =
    L"ChatViewOBS.ConfigChanged.v1";
inline constexpr wchar_t kToggleEditMessageName[] =
    L"ChatViewOBS.ToggleEdit.v1";
// Idempotent request: 1 means accepted by the HUD; 0 means refused.
inline constexpr wchar_t kOpenHudInteractionMessageName[] =
    L"ChatViewOBS.OpenHudInteraction.v1";
inline constexpr wchar_t kQueryHudHealthMessageName[] =
    L"ChatViewOBS.QueryHudHealth.v1";
inline constexpr wchar_t kControlCenterActivateMessageName[] =
    L"ChatViewOBS.ControlCenter.Activate.v1";

} // namespace chatview
