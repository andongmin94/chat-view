// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

#include <string>

namespace chatview {

inline constexpr int kDefaultHudWidthDip = 420;
inline constexpr int kDefaultHudHeightDip = 640;
inline constexpr int kMinimumHudWidthDip = 280;
inline constexpr int kMinimumHudHeightDip = 180;
inline constexpr int kMaximumHudWidthDip = 1200;
inline constexpr int kMaximumHudHeightDip = 1600;

struct HudPlacement {
    std::wstring monitor_device;
    int offset_x_dip = 0;
    int offset_y_dip = 0;
    int width_dip = kDefaultHudWidthDip;
    int height_dip = kDefaultHudHeightDip;
    bool valid = false;
};

[[nodiscard]] bool load_hud_placement(HudPlacement &placement) noexcept;
[[nodiscard]] bool save_hud_placement(const HudPlacement &placement) noexcept;
[[nodiscard]] RECT resolve_hud_bounds(const HudPlacement &placement, int margin_dip) noexcept;
[[nodiscard]] HudPlacement capture_hud_placement(HWND window) noexcept;
[[nodiscard]] SIZE minimum_hud_track_size(HWND window) noexcept;

} // namespace chatview
