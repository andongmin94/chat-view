// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/shared-state.hpp"

#include <Windows.h>

#include <cstdint>

namespace chatview {

class HudWindow final {
public:
    HudWindow() = default;
    ~HudWindow();

    HudWindow(const HudWindow &) = delete;
    HudWindow &operator=(const HudWindow &) = delete;

    [[nodiscard]] bool create(HINSTANCE instance);
    void destroy() noexcept;

    void show_ready();
    void apply_state(const SharedSnapshot &snapshot);

private:
    enum class DisplayMode {
        Hidden,
        Ready,
        Live,
        Recording,
        LiveAndRecording,
        Offline,
    };

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void render(DisplayMode mode);
    void hide();
    void arm_hide_timer(UINT milliseconds);
    void cancel_hide_timer();
    [[nodiscard]] UINT dpi() const noexcept;
    [[nodiscard]] const wchar_t *label_for(DisplayMode mode) const noexcept;

    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    DisplayMode mode_ = DisplayMode::Hidden;
    std::uint64_t last_generation_ = 0U;
    bool has_seen_active_state_ = false;
};

} // namespace chatview
