// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/shared-state.hpp"

#include <Windows.h>
#include <objidl.h>
#include <propidl.h>

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
        Editing,
    };

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void render(DisplayMode mode);
    void hide();
    void arm_hide_timer(UINT milliseconds);
    void cancel_hide_timer();
    void toggle_edit_mode();
    void set_click_through(bool enabled) noexcept;
    void capture_current_position() noexcept;
    [[nodiscard]] UINT dpi() const noexcept;
    [[nodiscard]] const wchar_t *label_for(DisplayMode mode) const noexcept;

    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    DisplayMode display_mode_ = DisplayMode::Hidden;
    DisplayMode output_mode_ = DisplayMode::Hidden;
    POINT position_{};
    std::uint64_t last_generation_ = 0U;
    bool has_custom_position_ = false;
    bool edit_mode_ = false;
    bool edit_hotkey_registered_ = false;
};

} // namespace chatview
