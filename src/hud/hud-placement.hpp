// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

#include <string>

namespace chatview {

struct HudPlacement {
    std::wstring monitor_device;
    LONG offset_x = 0;
    LONG offset_y = 0;
    bool valid = false;
};

bool load_hud_placement(HudPlacement &placement) noexcept;
[[nodiscard]] bool save_hud_placement(const HudPlacement &placement) noexcept;
[[nodiscard]] POINT resolve_hud_position(
    const HudPlacement &placement, int width, int height, int margin) noexcept;
[[nodiscard]] HudPlacement capture_hud_placement(HWND window) noexcept;

} // namespace chatview
