// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud/window-capture.hpp"
#include <string>
#include <vector>

namespace chatview {
class HudWindow;
// Companion-only, explicit window/output selection. No saved capture target,
// login, enrollment, global desktop capture, automatic monitor reconfiguration.
class VideoOutputPanel final {
public:
    VideoOutputPanel(HudWindow &hud, bool companion) noexcept;
    ~VideoOutputPanel();
    bool dispatch(MSG &message) noexcept;
    void tick() noexcept;
    void close() noexcept;
    DWORD wait_timeout() const noexcept;
private:
    struct Source { HWND window; DWORD process; std::wstring title; };
    struct Monitor { HMONITOR handle; MONITORINFOEXW info; };
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    void open() noexcept;
    void refresh();
    void start();
    void stop(const wchar_t *message) noexcept;
    void release() noexcept;
    bool permitted() const noexcept;
    bool topology(bool inspect_paths = false) const noexcept;
    void notice(const wchar_t *message) noexcept;
    HudWindow &hud_;
    bool enabled_ = false, hotkey_ = false, requested_ = false, releasing_ = false;
    HWND panel_ = nullptr, output_ = nullptr, cover_ = nullptr, source_ = nullptr;
    HMONITOR monitor_ = nullptr, source_monitor_ = nullptr;
    DWORD source_process_ = 0;
    RECT output_bounds_{};
    WindowCapture capture_;
    WindowCaptureStatus shown_ = WindowCaptureStatus::Stopped;
    std::vector<Source> sources_;
    std::vector<Monitor> monitors_;
};
}
