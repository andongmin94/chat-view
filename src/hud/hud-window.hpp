// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/hud-health.hpp"
#include "common/shared-state.hpp"
#include "hud/hud-placement.hpp"
#include "hud/page-connection-recovery.hpp"
#include "hud/page-health-watchdog.hpp"
#include "hud/page-health-message.hpp"
#include "hud/system-lifecycle.hpp"
#include "hud/webview-host.hpp"

#include <Windows.h>

#include <cstdint>
#include <string>

namespace chatview {

class HudWindow final {
public:
    HudWindow() = default;
    ~HudWindow();

    HudWindow(const HudWindow &) = delete;
    HudWindow &operator=(const HudWindow &) = delete;

    [[nodiscard]] bool create(HINSTANCE instance, HANDLE ready_event = nullptr);
    void destroy() noexcept;

    void show_ready();
    void apply_state(const SharedSnapshot &snapshot);

private:
    static LRESULT CALLBACK window_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT handle_message(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT hit_test(LPARAM lparam) const noexcept;
    void toggle_edit_mode();
    void apply_window_mode() noexcept;
    void reload_chat_config() noexcept;
    bool apply_page_health(
        const HudHealthSnapshot &health) noexcept;
    bool check_page_connection_recovery() noexcept;
    void check_page_health_watchdog() noexcept;
    void handle_system_lifecycle_action(
        SystemLifecycleAction action,
        std::uint16_t detail_code) noexcept;
    void pause_for_system_lifecycle(
        std::uint16_t detail_code) noexcept;
    void schedule_system_resume(
        std::uint16_t detail_code) noexcept;
    void complete_system_resume() noexcept;
    [[nodiscard]] bool system_suppressed() const noexcept;
    void handle_webview_process_failure(
        COREWEBVIEW2_PROCESS_FAILED_KIND kind) noexcept;
    void schedule_navigation_retry(
        COREWEBVIEW2_WEB_ERROR_STATUS status) noexcept;
    void cancel_navigation_retry() noexcept;
    [[nodiscard]] bool capture_exclusion_intact() const noexcept;
    void fail_closed_capture_exclusion() noexcept;
    bool apply_capture_policy() noexcept;
    void update_host_state() noexcept;
    void set_transient_status(
        std::wstring text, std::wstring tone, UINT duration_ms);
    void clear_transient_status() noexcept;
    void capture_and_persist_bounds() noexcept;
    void restore_saved_bounds() noexcept;
    void set_page_health(
        HudPageState state,
        HudProvider provider,
        std::uint16_t detail_code = 0U) noexcept;
    [[nodiscard]] HudHealthSnapshot page_health() const noexcept;
    [[nodiscard]] UINT dpi() const noexcept;

    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HANDLE ready_event_ = nullptr;
    UINT config_changed_message_ = 0U;
    UINT toggle_edit_message_ = 0U;
    UINT query_health_message_ = 0U;
    WebViewHost webview_;
    HudPlacement placement_;
    std::wstring transient_status_;
    std::wstring transient_tone_ = L"#aeb0b2";
    std::wstring navigation_status_;
    std::wstring navigation_tone_ = L"#ffcc00";
    std::uint64_t last_generation_ = 0U;
    unsigned int navigation_retry_attempt_ = 0U;
    PageConnectionRecovery page_connection_recovery_;
    PageHealthWatchdog page_health_watchdog_;
    SystemLifecycle system_lifecycle_;
    HudPageState page_state_ = HudPageState::Starting;
    HudProvider page_provider_ = HudProvider::Unknown;
    std::uint16_t page_detail_code_ = 0U;
    bool streaming_ = false;
    bool recording_ = false;
    bool capture_risk_ = false;
    bool edit_mode_ = false;
    bool edit_hotkey_registered_ = false;
    bool session_notifications_registered_ = false;
    bool webview_ready_ = false;
    bool system_resume_pending_ = false;
    bool restart_after_system_resume_ = false;
    bool capture_exclusion_failed_ = false;
};

} // namespace chatview
