// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud/window-capture.hpp"
#include "hud/video-output-check.hpp"
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
    friend struct VideoOutputPanelTestAccess;
    struct Source { HWND window; DWORD process; DWORD thread; std::wstring title; };
    struct Monitor { HMONITOR handle; MONITORINFOEXW info; };
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    void open() noexcept;
    void refresh();
    void identify();
    void show_pattern();
    void paint_cover(HWND window) noexcept;
    void start();
    void begin_capture();
    void update_capture(const WindowCaptureSnapshot &value) noexcept;
    void stop(const wchar_t *message) noexcept;
    void release() noexcept;
    void interrupt(const wchar_t *message) noexcept;
    void invalidate_choices() noexcept;
    void ensure_output();
    bool output_intact() const noexcept;
    bool mask() noexcept;
    bool permitted() const noexcept;
    bool guard_protection() noexcept;
    bool topology(bool inspect_paths = false) const noexcept;
    void notice(const wchar_t *message) noexcept;
    void show_step() noexcept;
    HudWindow &hud_;
    UINT open_message_ = 0U;
    bool enabled_ = false, hotkey_ = false, requested_ = false, releasing_ = false;
    bool notifications_ = false, session_blocked_ = false, suspended_ = false;
    bool confirming_ = false, closing_ = false;
    std::uint64_t selection_epoch_ = 0;
    ULONGLONG next_path_check_ = 0;
    HWND panel_ = nullptr, output_ = nullptr, cover_ = nullptr, source_ = nullptr;
    HMONITOR monitor_ = nullptr, source_monitor_ = nullptr;
    DWORD source_process_ = 0, source_thread_ = 0;
    std::wstring output_device_;
    RECT output_bounds_{};
    VideoOutputCheck check_;
    WindowCapture capture_;
    WindowCaptureStatus shown_ = WindowCaptureStatus::Stopped;
    std::vector<Source> sources_;
    std::vector<Monitor> monitors_;
};
}
