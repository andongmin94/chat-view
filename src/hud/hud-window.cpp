// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/hud-window.hpp"

#include "common/chat-config.hpp"
#include "common/window-messages.hpp"

#include <Windows.h>
#include <WtsApi32.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace chatview {
namespace {

constexpr UINT_PTR kStatusTimerId = 1U;
constexpr UINT_PTR kNavigationRetryTimerId = 2U;
constexpr UINT_PTR kCaptureSafetyTimerId = 3U;
constexpr UINT_PTR kSystemResumeTimerId = 4U;
constexpr int kEditHotkeyId = 1;
constexpr int kCaptureExclusionLostExitCode = 11;
constexpr int kPageHealthWatchdogExitCode = 19;
constexpr int kPageConnectionRecoveryExitCode = 20;
constexpr int kSystemLifecycleRestartExitCode = 21;
constexpr std::uint16_t kPageHealthWatchdogDetailBase = 0x7100U;
constexpr std::uint16_t kPageConnectionRecoveryDetailBase = 0x7200U;
constexpr std::uint16_t kSystemLifecycleDetailBase = 0x7300U;
constexpr std::uint16_t kPowerSuspendedDetail =
    kSystemLifecycleDetailBase + 1U;
constexpr std::uint16_t kPowerResumedDetail =
    kSystemLifecycleDetailBase + 2U;
constexpr std::uint16_t kSessionLockedDetail =
    kSystemLifecycleDetailBase + 3U;
constexpr std::uint16_t kSessionUnlockedDetail =
    kSystemLifecycleDetailBase + 4U;
constexpr std::uint16_t kSessionEndingDetail =
    kSystemLifecycleDetailBase + 5U;
constexpr std::uint16_t kSessionEndCancelledDetail =
    kSystemLifecycleDetailBase + 6U;
constexpr UINT kReadyDurationMs = 2200U;
constexpr UINT kOfflineDurationMs = 1200U;
constexpr UINT kNavigationRetryBaseMs = 5000U;
constexpr UINT kNavigationRetryMaximumMs = 30000U;
constexpr UINT kCaptureSafetyIntervalMs = 1000U;
constexpr UINT kSystemResumeDelayMs = 1200U;
constexpr UINT kEditHotkeyModifiers =
    MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
constexpr UINT kEditHotkeyVirtualKey = 'H';
constexpr int kDefaultMarginDip = 24;
constexpr int kResizeBorderDip = 10;
constexpr int kDragHeaderDip = 46;

#ifndef WDA_EXCLUDEFROMCAPTURE
constexpr DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
#endif

void debug_windows_error(const wchar_t *operation)
{
    const DWORD error = GetLastError();
    wchar_t message[256]{};
    swprintf_s(
        message,
        L"[ChatView HUD] %s failed with error %lu\n",
        operation,
        static_cast<unsigned long>(error));
    OutputDebugStringW(message);
}

bool set_window_long_checked(
    HWND window, int index, LONG_PTR value) noexcept
{
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window, index, value);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

HudProvider provider_for_url(const std::wstring &url) noexcept
{
    if (url.starts_with(L"https://weflab.com/")) {
        return HudProvider::Weflab;
    }
    if (url.starts_with(L"https://chzzk.naver.com/")) {
        return HudProvider::Chzzk;
    }
    if (url.starts_with(L"https://play.sooplive.com/")) {
        return HudProvider::Soop;
    }
    if (url.starts_with(L"https://www.youtube.com/")) {
        return HudProvider::YouTube;
    }
    return HudProvider::Unknown;
}

template<typename Enum>
std::uint16_t detail_code(Enum value) noexcept
{
    const auto numeric = static_cast<unsigned int>(value);
    return static_cast<std::uint16_t>(
        std::min(numeric, static_cast<unsigned int>(0xffffU)));
}

} // namespace

HudWindow::~HudWindow()
{
    destroy();
}

bool HudWindow::create(HINSTANCE instance, HANDLE ready_event)
{
    instance_ = instance;
    ready_event_ = ready_event;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &HudWindow::window_proc;
    window_class.hInstance = instance_;
    window_class.lpszClassName = kHudWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (RegisterClassExW(&window_class) == 0U &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        debug_windows_error(L"RegisterClassExW");
        return false;
    }

    HudPlacement loaded;
    if (load_hud_placement(loaded)) {
        placement_ = std::move(loaded);
    }
    const RECT bounds = resolve_hud_bounds(
        placement_, kDefaultMarginDip);

    constexpr DWORD extended_style =
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT |
        WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP;
    window_ = CreateWindowExW(
        extended_style,
        kHudWindowClassName,
        L"ChatView HUD",
        WS_POPUP,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        debug_windows_error(L"CreateWindowExW");
        return false;
    }

    session_notifications_registered_ =
        WTSRegisterSessionNotification(
            window_, NOTIFY_FOR_THIS_SESSION) != FALSE;
    if (!session_notifications_registered_) {
        debug_windows_error(L"WTSRegisterSessionNotification");
    }

    if (!SetWindowDisplayAffinity(
            window_, WDA_EXCLUDEFROMCAPTURE)) {
        debug_windows_error(L"SetWindowDisplayAffinity");
        return false;
    }
    if (!capture_exclusion_intact()) {
        OutputDebugStringW(
            L"[ChatView HUD] Capture exclusion was not retained; "
            L"refusing to initialize the HUD\n");
        return false;
    }

    config_changed_message_ = RegisterWindowMessageW(
        kConfigChangedMessageName);
    toggle_edit_message_ = RegisterWindowMessageW(
        kToggleEditMessageName);
    query_health_message_ = RegisterWindowMessageW(
        kQueryHudHealthMessageName);
    if (config_changed_message_ == 0U ||
        toggle_edit_message_ == 0U ||
        query_health_message_ == 0U) {
        debug_windows_error(L"RegisterWindowMessageW");
        return false;
    }

    edit_hotkey_registered_ =
        RegisterHotKey(
            window_,
            kEditHotkeyId,
            kEditHotkeyModifiers,
            kEditHotkeyVirtualKey) != FALSE;
    if (!edit_hotkey_registered_) {
        debug_windows_error(L"RegisterHotKey");
    }

    if (!webview_.initialize(window_)) {
        return false;
    }

    if (SetTimer(
            window_,
            kCaptureSafetyTimerId,
            kCaptureSafetyIntervalMs,
            nullptr) == 0U) {
        debug_windows_error(L"SetTimer(capture safety)");
        return false;
    }

    set_page_health(
        HudPageState::Starting, HudProvider::Unknown);
    ShowWindow(window_, SW_HIDE);
    return true;
}

void HudWindow::destroy() noexcept
{
    if (window_ != nullptr) {
        KillTimer(window_, kStatusTimerId);
        KillTimer(window_, kNavigationRetryTimerId);
        KillTimer(window_, kCaptureSafetyTimerId);
        KillTimer(window_, kSystemResumeTimerId);
    }

    if (window_ != nullptr && session_notifications_registered_) {
        WTSUnRegisterSessionNotification(window_);
        session_notifications_registered_ = false;
    }

    if (window_ != nullptr && edit_hotkey_registered_) {
        UnregisterHotKey(window_, kEditHotkeyId);
        edit_hotkey_registered_ = false;
    }

    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();
    system_lifecycle_.reset();
    system_resume_pending_ = false;
    restart_after_system_resume_ = false;
    webview_.close();
    ready_event_ = nullptr;
    if (window_ != nullptr) {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0L);
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (instance_ != nullptr) {
        UnregisterClassW(kHudWindowClassName, instance_);
        instance_ = nullptr;
    }
}

void HudWindow::show_ready()
{
    set_transient_status(
        L"READY", L"#5ac8fa", kReadyDurationMs);
}

void HudWindow::apply_state(const SharedSnapshot &snapshot)
{
    if (snapshot.generation == last_generation_) {
        return;
    }
    last_generation_ = snapshot.generation;

    if (has_flag(snapshot, SharedStateShutdown)) {
        PostQuitMessage(0);
        return;
    }

    const bool was_active = streaming_ || recording_;
    streaming_ = has_flag(snapshot, SharedStateStreaming);
    recording_ = has_flag(snapshot, SharedStateRecording);
    capture_risk_ = has_flag(snapshot, SharedStateCaptureRisk);
    const bool active = streaming_ || recording_;

    if (active) {
        clear_transient_status();
    } else if (was_active) {
        set_transient_status(
            L"OFFLINE", L"#aeb0b2", kOfflineDurationMs);
    } else {
        update_host_state();
    }

    apply_capture_policy();
}

LRESULT CALLBACK HudWindow::window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    HudWindow *self =
        reinterpret_cast<HudWindow *>(
            GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create =
            reinterpret_cast<const CREATESTRUCTW *>(lparam);
        self = static_cast<HudWindow *>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }

    return self != nullptr
               ? self->handle_message(
                     window, message, wparam, lparam)
               : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT HudWindow::handle_message(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (config_changed_message_ != 0U &&
        message == config_changed_message_) {
        if (!system_suppressed()) {
            reload_chat_config();
        }
        return 0L;
    }
    if (toggle_edit_message_ != 0U &&
        message == toggle_edit_message_) {
        toggle_edit_mode();
        return 0L;
    }
    if (query_health_message_ != 0U &&
        message == query_health_message_) {
        return static_cast<LRESULT>(
            encode_hud_health(page_health()));
    }

    switch (message) {
    case kWebViewReadyMessage:
        webview_ready_ = true;
        if (!system_suppressed()) {
            reload_chat_config();
        }
        update_host_state();
        if (!capture_exclusion_intact()) {
            fail_closed_capture_exclusion();
            return 0L;
        }
        if (!apply_capture_policy()) {
            return 0L;
        }
        if (ready_event_ != nullptr &&
            !SetEvent(ready_event_)) {
            debug_windows_error(L"SetEvent(ready)");
            set_page_health(
                HudPageState::Fatal,
                page_provider_,
                static_cast<std::uint16_t>(13U));
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(13);
        }
        return 0L;
    case kWebViewDocumentReadyMessage:
        if (system_suppressed()) {
            return 0L;
        }
        cancel_navigation_retry();
        if (page_state_ != HudPageState::SetupRequired) {
            apply_page_health(HudHealthSnapshot{
                HudPageState::Loading,
                page_provider_,
                0U});
        } else {
            update_host_state();
        }
        return 0L;
    case kWebViewPageHealthMessage: {
        if (system_suppressed()) {
            page_connection_recovery_.reset();
            page_health_watchdog_.disarm();
            return 0L;
        }
        const HudHealthSnapshot health = decode_hud_health(
            static_cast<std::uint32_t>(wparam));
        if (apply_page_health(health)) {
            const ULONGLONG now = GetTickCount64();
            if (capture_risk_) {
                page_connection_recovery_.reset();
                page_health_watchdog_.disarm();
            } else if (health.state == HudPageState::NetworkOffline) {
                page_connection_recovery_.reset();
                page_health_watchdog_.disarm();
            } else {
                if (page_health_watchdog_.armed()) {
                    page_health_watchdog_.heartbeat(now);
                } else {
                    page_health_watchdog_.arm(now);
                }
                if (health.state == HudPageState::ConnectionLost) {
                    page_connection_recovery_.begin(now);
                }
            }
        }
        return 0L;
    }
    case kWebViewProcessFailedMessage:
        handle_webview_process_failure(
            static_cast<COREWEBVIEW2_PROCESS_FAILED_KIND>(wparam));
        return 0L;
    case kWebViewNavigationFailedMessage:
        schedule_navigation_retry(
            static_cast<COREWEBVIEW2_WEB_ERROR_STATUS>(wparam));
        return 0L;
    case kWebViewFailedMessage: {
        if (system_suppressed()) {
            restart_after_system_resume_ = true;
            OutputDebugStringW(
                L"[ChatView HUD] WebView host failed while Windows was "
                L"paused; deferring restart until resume\n");
            return 0L;
        }
        wchar_t detail[192]{};
        swprintf_s(
            detail,
            L"[ChatView HUD] WebView2 host failed (0x%08lX)\n",
            static_cast<unsigned long>(
                static_cast<std::uint32_t>(wparam)));
        OutputDebugStringW(detail);
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(wparam) & 0xffffU));
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(2);
        return 0L;
    }
    case WM_TIMER:
        if (wparam == kStatusTimerId) {
            clear_transient_status();
            return 0L;
        }
        if (wparam == kNavigationRetryTimerId) {
            KillTimer(window_, kNavigationRetryTimerId);
            if (system_suppressed()) {
                return 0L;
            }
            page_connection_recovery_.reset();
            set_page_health(
                HudPageState::Loading, page_provider_);
            if (capture_risk_) {
                page_health_watchdog_.disarm();
            } else {
                page_health_watchdog_.arm(GetTickCount64());
            }
            if (!webview_.reload()) {
                page_health_watchdog_.disarm();
                set_page_health(
                    HudPageState::Fatal,
                    page_provider_,
                    static_cast<std::uint16_t>(10U));
                ShowWindow(window_, SW_HIDE);
                PostQuitMessage(10);
            }
            return 0L;
        }
        if (wparam == kSystemResumeTimerId) {
            complete_system_resume();
            return 0L;
        }
        if (wparam == kCaptureSafetyTimerId) {
            if (!capture_risk_ && !system_suppressed()) {
                if (!capture_exclusion_intact()) {
                    fail_closed_capture_exclusion();
                } else if (!check_page_connection_recovery()) {
                    check_page_health_watchdog();
                }
            }
            return 0L;
        }
        break;
    case WM_POWERBROADCAST:
        switch (static_cast<UINT>(wparam)) {
        case PBT_APMSUSPEND:
            handle_system_lifecycle_action(
                system_lifecycle_.suspend(),
                kPowerSuspendedDetail);
            break;
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMECRITICAL:
        case PBT_APMRESUMESUSPEND:
            handle_system_lifecycle_action(
                system_lifecycle_.resume(),
                kPowerResumedDetail);
            break;
        default:
            break;
        }
        return TRUE;
    case WM_WTSSESSION_CHANGE:
        if (wparam == WTS_SESSION_LOCK) {
            handle_system_lifecycle_action(
                system_lifecycle_.lock_session(),
                kSessionLockedDetail);
        } else if (wparam == WTS_SESSION_UNLOCK) {
            handle_system_lifecycle_action(
                system_lifecycle_.unlock_session(),
                kSessionUnlockedDetail);
        }
        return 0L;
    case WM_QUERYENDSESSION:
        handle_system_lifecycle_action(
            system_lifecycle_.begin_end_session(),
            kSessionEndingDetail);
        return TRUE;
    case WM_ENDSESSION:
        if (wparam != FALSE) {
            KillTimer(window_, kSystemResumeTimerId);
            page_connection_recovery_.reset();
            page_health_watchdog_.disarm();
            system_lifecycle_.reset();
            system_resume_pending_ = false;
            restart_after_system_resume_ = false;
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(0);
        } else {
            handle_system_lifecycle_action(
                system_lifecycle_.cancel_end_session(),
                kSessionEndCancelledDetail);
        }
        return 0L;
    case WM_HOTKEY:
        if (wparam == static_cast<WPARAM>(kEditHotkeyId)) {
            toggle_edit_mode();
            return 0L;
        }
        break;
    case WM_NCHITTEST:
        return hit_test(lparam);
    case WM_MOUSEACTIVATE:
        return edit_mode_ ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_GETMINMAXINFO: {
        auto *minmax = reinterpret_cast<MINMAXINFO *>(lparam);
        const SIZE minimum = minimum_hud_track_size(window_);
        const UINT current_dpi = dpi();
        minmax->ptMinTrackSize.x = minimum.cx;
        minmax->ptMinTrackSize.y = minimum.cy;
        minmax->ptMaxTrackSize.x = MulDiv(
            kMaximumHudWidthDip,
            static_cast<int>(current_dpi),
            96);
        minmax->ptMaxTrackSize.y = MulDiv(
            kMaximumHudHeightDip,
            static_cast<int>(current_dpi),
            96);
        return 0L;
    }
    case WM_ENTERSIZEMOVE:
        return 0L;
    case WM_EXITSIZEMOVE:
        if (edit_mode_) {
            capture_and_persist_bounds();
        }
        if (!system_suppressed() &&
            !capture_exclusion_intact()) {
            fail_closed_capture_exclusion();
        }
        return 0L;
    case WM_MOVE:
        webview_.notify_parent_position_changed();
        return 0L;
    case WM_SIZE:
        webview_.resize();
        return 0L;
    case WM_DPICHANGED: {
        const RECT *suggested =
            reinterpret_cast<const RECT *>(lparam);
        if (!SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE)) {
            debug_windows_error(L"SetWindowPos(DPI change)");
            set_page_health(
                HudPageState::Fatal,
                page_provider_,
                static_cast<std::uint16_t>(14U));
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(14);
            return 0L;
        }
        webview_.resize();
        if (edit_mode_) {
            capture_and_persist_bounds();
        }
        if (!system_suppressed() &&
            !capture_exclusion_intact()) {
            fail_closed_capture_exclusion();
        }
        return 0L;
    }
    case WM_DISPLAYCHANGE:
        if (!system_suppressed()) {
            restore_saved_bounds();
        }
        return 0L;
    case WM_LBUTTONDOWN:
        if (edit_mode_) {
            webview_.focus();
        }
        [[fallthrough]];
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (edit_mode_ &&
            webview_.forward_mouse_message(
                message, wparam, lparam)) {
            return 0L;
        }
        break;
    case WM_CLOSE:
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(0);
        return 0L;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0L;
    default:
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT HudWindow::hit_test(LPARAM lparam) const noexcept
{
    if (!edit_mode_ || window_ == nullptr) {
        return HTTRANSPARENT;
    }

    RECT bounds{};
    if (!GetWindowRect(window_, &bounds)) {
        return HTCLIENT;
    }

    const int border = std::max(
        6,
        MulDiv(
            kResizeBorderDip,
            static_cast<int>(dpi()),
            96));
    const int header = std::max(
        28,
        MulDiv(
            kDragHeaderDip,
            static_cast<int>(dpi()),
            96));
    const POINT point{
        GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    const bool left = point.x < bounds.left + border;
    const bool right = point.x >= bounds.right - border;
    const bool top = point.y < bounds.top + border;
    const bool bottom = point.y >= bounds.bottom - border;

    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }
    if (point.y < bounds.top + header) {
        return HTCAPTION;
    }
    return HTCLIENT;
}

void HudWindow::toggle_edit_mode()
{
    if (window_ == nullptr || !webview_ready_ ||
        capture_exclusion_failed_ || capture_risk_ ||
        system_suppressed()) {
        return;
    }

    if (edit_mode_) {
        capture_and_persist_bounds();
    }
    edit_mode_ = !edit_mode_;
    apply_window_mode();
    if (capture_exclusion_failed_) {
        return;
    }
    update_host_state();

    if (edit_mode_) {
        ShowWindow(window_, SW_SHOW);
        if (!capture_exclusion_intact()) {
            fail_closed_capture_exclusion();
            return;
        }
        SetForegroundWindow(window_);
        webview_.focus();
    }
}

void HudWindow::apply_window_mode() noexcept
{
    if (window_ == nullptr || capture_exclusion_failed_) {
        return;
    }

    LONG_PTR style = GetWindowLongPtrW(window_, GWL_STYLE);
    LONG_PTR extended_style =
        GetWindowLongPtrW(window_, GWL_EXSTYLE);
    if (edit_mode_) {
        style |= WS_THICKFRAME;
        extended_style &= ~static_cast<LONG_PTR>(
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    } else {
        style &= ~static_cast<LONG_PTR>(WS_THICKFRAME);
        extended_style |=
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    }

    if (!set_window_long_checked(
            window_, GWL_STYLE, style)) {
        debug_windows_error(L"SetWindowLongPtrW(style)");
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            static_cast<std::uint16_t>(15U));
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(15);
        return;
    }
    if (!set_window_long_checked(
            window_, GWL_EXSTYLE, extended_style)) {
        debug_windows_error(
            L"SetWindowLongPtrW(extended style)");
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            static_cast<std::uint16_t>(16U));
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(16);
        return;
    }
    if (!SetWindowPos(
            window_,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE |
                SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
        debug_windows_error(L"SetWindowPos(window mode)");
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            static_cast<std::uint16_t>(17U));
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(17);
        return;
    }

    if (!capture_exclusion_intact()) {
        fail_closed_capture_exclusion();
        return;
    }
    webview_.resize();
}

void HudWindow::reload_chat_config() noexcept
{
    if (!webview_ready_ || capture_exclusion_failed_ ||
        system_suppressed()) {
        return;
    }

    cancel_navigation_retry();
    page_connection_recovery_.reset();

    ChatConfig config;
    if (load_chat_config(config)) {
        const HudProvider provider = provider_for_url(config.url);
        set_page_health(HudPageState::Loading, provider);
        if (capture_risk_) {
            page_health_watchdog_.disarm();
        } else {
            page_health_watchdog_.arm(GetTickCount64());
        }
        if (!webview_.navigate(config.url)) {
            page_health_watchdog_.disarm();
            schedule_navigation_retry(
                COREWEBVIEW2_WEB_ERROR_STATUS_UNEXPECTED_ERROR);
        }
    } else {
        page_health_watchdog_.disarm();
        set_page_health(
            HudPageState::SetupRequired,
            HudProvider::Unknown);
        webview_.show_setup_page();
    }
}

bool HudWindow::apply_page_health(
    const HudHealthSnapshot &health) noexcept
{
    if (!is_valid_hud_health(health) ||
        !is_dom_reportable_page_state(health.state) ||
        health.provider == HudProvider::Unknown ||
        health.provider != page_provider_) {
        return false;
    }

    if (page_state_ == health.state &&
        page_provider_ == health.provider &&
        page_detail_code_ == health.detail_code) {
        return true;
    }

    set_page_health(
        health.state, health.provider, health.detail_code);
    switch (health.state) {
    case HudPageState::Loading:
        navigation_status_ = L"CHAT LOADING";
        navigation_tone_ = L"#ffcc00";
        break;
    case HudPageState::Ready:
        page_connection_recovery_.reset();
        cancel_navigation_retry();
        navigation_status_.clear();
        navigation_tone_ = L"#ffcc00";
        break;
    case HudPageState::LoginRequired:
        page_connection_recovery_.reset();
        cancel_navigation_retry();
        navigation_status_ = L"LOGIN REQUIRED";
        navigation_tone_ = L"#ffcc00";
        break;
    case HudPageState::Offline:
        page_connection_recovery_.reset();
        cancel_navigation_retry();
        navigation_status_ = L"BROADCAST OFFLINE";
        navigation_tone_ = L"#aeb0b2";
        break;
    case HudPageState::LayoutChanged:
        page_connection_recovery_.reset();
        cancel_navigation_retry();
        navigation_status_ = L"CHAT PAGE CHANGED";
        navigation_tone_ = L"#ff3b30";
        break;
    case HudPageState::NetworkOffline:
        page_connection_recovery_.reset();
        page_health_watchdog_.disarm();
        cancel_navigation_retry();
        navigation_status_ = L"NETWORK OFFLINE";
        navigation_tone_ = L"#ff9500";
        break;
    case HudPageState::ConnectionLost:
        cancel_navigation_retry();
        navigation_status_ = L"CHAT DISCONNECTED";
        navigation_tone_ = L"#ff9500";
        break;
    default:
        return false;
    }
    update_host_state();
    return true;
}

void HudWindow::handle_webview_process_failure(
    COREWEBVIEW2_PROCESS_FAILED_KIND kind) noexcept
{
    if (system_suppressed()) {
        restart_after_system_resume_ = true;
        OutputDebugStringW(
            L"[ChatView HUD] WebView process failed while Windows was "
            L"paused; deferring restart until resume\n");
        return;
    }

    wchar_t detail[160]{};
    swprintf_s(
        detail,
        L"[ChatView HUD] WebView2 process failure kind %u\n",
        static_cast<unsigned int>(kind));
    OutputDebugStringW(detail);

    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();
    set_page_health(
        HudPageState::Recovering,
        page_provider_,
        detail_code(kind));

    switch (kind) {
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED:
    case COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED:
        navigation_status_ = L"CHAT RECOVERING";
        navigation_tone_ = L"#5ac8fa";
        update_host_state();
        if (!capture_risk_) {
            page_health_watchdog_.arm(GetTickCount64());
        }
        if (!webview_.reload()) {
            page_health_watchdog_.disarm();
            set_page_health(
                HudPageState::Fatal,
                page_provider_,
                detail_code(kind));
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(9);
        }
        return;
    case COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED:
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE:
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            detail_code(kind));
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(9);
        return;
    default:
        if (!capture_risk_) {
            page_health_watchdog_.arm(GetTickCount64());
        }
        return;
    }
}

void HudWindow::schedule_navigation_retry(
    COREWEBVIEW2_WEB_ERROR_STATUS status) noexcept
{
    if (system_suppressed()) {
        return;
    }

    wchar_t detail[160]{};
    swprintf_s(
        detail,
        L"[ChatView HUD] Chat navigation failed with status %u\n",
        static_cast<unsigned int>(status));
    OutputDebugStringW(detail);

    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();
    set_page_health(
        HudPageState::Retrying,
        page_provider_,
        detail_code(status));
    navigation_status_ = L"CHAT RETRYING";
    navigation_tone_ = L"#ffcc00";
    navigation_retry_attempt_ = std::min(
        navigation_retry_attempt_ + 1U, 4U);

    const unsigned int shift = std::min(
        navigation_retry_attempt_ - 1U, 3U);
    const UINT delay = std::min(
        kNavigationRetryBaseMs << shift,
        kNavigationRetryMaximumMs);

    KillTimer(window_, kNavigationRetryTimerId);
    if (SetTimer(
            window_,
            kNavigationRetryTimerId,
            delay,
            nullptr) == 0U) {
        debug_windows_error(L"SetTimer(navigation retry)");
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            detail_code(status));
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(10);
        return;
    }
    update_host_state();
}

void HudWindow::cancel_navigation_retry() noexcept
{
    if (window_ != nullptr) {
        KillTimer(window_, kNavigationRetryTimerId);
    }
    navigation_retry_attempt_ = 0U;
    navigation_status_.clear();
    navigation_tone_ = L"#ffcc00";
}

void HudWindow::handle_system_lifecycle_action(
    SystemLifecycleAction action,
    std::uint16_t detail) noexcept
{
    switch (action) {
    case SystemLifecycleAction::Pause:
        pause_for_system_lifecycle(detail);
        break;
    case SystemLifecycleAction::Resume:
        schedule_system_resume(detail);
        break;
    case SystemLifecycleAction::None:
    default:
        break;
    }
}

void HudWindow::pause_for_system_lifecycle(
    std::uint16_t detail) noexcept
{
    if (window_ == nullptr) {
        return;
    }

    KillTimer(window_, kSystemResumeTimerId);
    KillTimer(window_, kNavigationRetryTimerId);
    system_resume_pending_ = false;
    navigation_retry_attempt_ = 0U;
    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();

    if (edit_mode_) {
        capture_and_persist_bounds();
        edit_mode_ = false;
    }

    set_page_health(
        HudPageState::SystemPaused, page_provider_, detail);
    navigation_status_ = L"WINDOWS PAUSED";
    navigation_tone_ = L"#aeb0b2";
    update_host_state();
    ShowWindow(window_, SW_HIDE);
}

void HudWindow::schedule_system_resume(
    std::uint16_t detail) noexcept
{
    if (window_ == nullptr) {
        return;
    }

    KillTimer(window_, kSystemResumeTimerId);
    KillTimer(window_, kNavigationRetryTimerId);
    system_resume_pending_ = true;
    navigation_retry_attempt_ = 0U;
    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();
    set_page_health(
        HudPageState::SystemResuming, page_provider_, detail);
    navigation_status_ = L"WINDOWS RESUMING";
    navigation_tone_ = L"#5ac8fa";
    update_host_state();
    ShowWindow(window_, SW_HIDE);

    if (SetTimer(
            window_,
            kSystemResumeTimerId,
            kSystemResumeDelayMs,
            nullptr) == 0U) {
        debug_windows_error(L"SetTimer(system resume)");
        system_resume_pending_ = false;
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            static_cast<std::uint16_t>(
                kSystemLifecycleRestartExitCode));
        PostQuitMessage(kSystemLifecycleRestartExitCode);
    }
}

void HudWindow::complete_system_resume() noexcept
{
    if (window_ == nullptr) {
        return;
    }

    KillTimer(window_, kSystemResumeTimerId);
    if (system_lifecycle_.paused()) {
        return;
    }

    system_resume_pending_ = false;
    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();
    cancel_navigation_retry();

    if (restart_after_system_resume_) {
        restart_after_system_resume_ = false;
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            static_cast<std::uint16_t>(
                kSystemLifecycleRestartExitCode));
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(kSystemLifecycleRestartExitCode);
        return;
    }

    if (!webview_ready_) {
        set_page_health(
            HudPageState::SystemResuming,
            page_provider_,
            kPowerResumedDetail);
        return;
    }

    if (!SetWindowDisplayAffinity(
            window_, WDA_EXCLUDEFROMCAPTURE)) {
        debug_windows_error(
            L"SetWindowDisplayAffinity(system resume)");
        fail_closed_capture_exclusion();
        return;
    }
    if (!capture_exclusion_intact()) {
        fail_closed_capture_exclusion();
        return;
    }

    edit_mode_ = false;
    apply_window_mode();
    if (capture_exclusion_failed_) {
        return;
    }
    restore_saved_bounds();
    if (capture_exclusion_failed_) {
        return;
    }

    webview_.notify_parent_position_changed();
    webview_.resize();
    reload_chat_config();
    apply_capture_policy();
}

bool HudWindow::system_suppressed() const noexcept
{
    return system_lifecycle_.paused() || system_resume_pending_;
}

bool HudWindow::check_page_connection_recovery() noexcept
{
    if (window_ == nullptr || !webview_ready_ || capture_risk_ ||
        capture_exclusion_failed_ || system_suppressed() ||
        page_provider_ == HudProvider::Unknown) {
        return false;
    }

    const PageConnectionRecoveryAction action =
        page_connection_recovery_.poll(GetTickCount64());
    if (action == PageConnectionRecoveryAction::None) {
        return false;
    }

    const std::uint16_t detail = static_cast<std::uint16_t>(
        kPageConnectionRecoveryDetailBase +
        (action == PageConnectionRecoveryAction::Reload ? 1U : 2U));

    KillTimer(window_, kNavigationRetryTimerId);
    navigation_retry_attempt_ = 0U;

    if (action == PageConnectionRecoveryAction::Reload) {
        OutputDebugStringW(
            L"[ChatView HUD] Chat connection remained disconnected; "
            L"reloading the page\n");
        set_page_health(
            HudPageState::Recovering, page_provider_, detail);
        navigation_status_ = L"CHAT RECONNECTING";
        navigation_tone_ = L"#5ac8fa";
        page_health_watchdog_.arm(GetTickCount64());
        update_host_state();
        if (!webview_.reload()) {
            page_connection_recovery_.reset();
            page_health_watchdog_.disarm();
            set_page_health(
                HudPageState::Fatal, page_provider_, detail);
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(kPageConnectionRecoveryExitCode);
        }
        return true;
    }

    OutputDebugStringW(
        L"[ChatView HUD] Chat connection remained disconnected after reload; "
        L"restarting the HUD process\n");
    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();
    set_page_health(
        HudPageState::Fatal, page_provider_, detail);
    navigation_status_ = L"CHAT DISCONNECTED";
    navigation_tone_ = L"#ff3b30";
    update_host_state();
    ShowWindow(window_, SW_HIDE);
    PostQuitMessage(kPageConnectionRecoveryExitCode);
    return true;
}

void HudWindow::check_page_health_watchdog() noexcept
{
    if (window_ == nullptr || !webview_ready_ || capture_risk_ ||
        capture_exclusion_failed_ || system_suppressed() ||
        page_provider_ == HudProvider::Unknown) {
        return;
    }

    const PageHealthWatchdogAction action =
        page_health_watchdog_.poll(GetTickCount64());
    if (action == PageHealthWatchdogAction::None) {
        return;
    }

    const unsigned int timeout_count =
        page_health_watchdog_.consecutive_timeouts();
    const std::uint16_t detail = static_cast<std::uint16_t>(
        kPageHealthWatchdogDetailBase +
        std::min(timeout_count, 0xffU));

    KillTimer(window_, kNavigationRetryTimerId);
    navigation_retry_attempt_ = 0U;

    if (action == PageHealthWatchdogAction::Reload) {
        wchar_t message[192]{};
        swprintf_s(
            message,
            L"[ChatView HUD] Page-health heartbeat timed out; "
            L"reloading chat (%u/%u)\n",
            timeout_count,
            PageHealthWatchdog::kMaximumReloadAttempts);
        OutputDebugStringW(message);

        set_page_health(
            HudPageState::Recovering, page_provider_, detail);
        navigation_status_ = L"CHAT RECOVERING";
        navigation_tone_ = L"#5ac8fa";
        update_host_state();
        if (!webview_.reload()) {
            page_health_watchdog_.disarm();
            set_page_health(
                HudPageState::Fatal, page_provider_, detail);
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(kPageHealthWatchdogExitCode);
        }
        return;
    }

    OutputDebugStringW(
        L"[ChatView HUD] Page-health heartbeat remained stale; "
        L"restarting the HUD process\n");
    set_page_health(
        HudPageState::Fatal, page_provider_, detail);
    navigation_status_ = L"CHAT UNRESPONSIVE";
    navigation_tone_ = L"#ff3b30";
    update_host_state();
    ShowWindow(window_, SW_HIDE);
    PostQuitMessage(kPageHealthWatchdogExitCode);
}

bool HudWindow::capture_exclusion_intact() const noexcept
{
    if (window_ == nullptr) {
        return false;
    }

    DWORD affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window_, &affinity)) {
        debug_windows_error(L"GetWindowDisplayAffinity");
        return false;
    }
    return affinity == WDA_EXCLUDEFROMCAPTURE;
}

void HudWindow::fail_closed_capture_exclusion() noexcept
{
    if (capture_exclusion_failed_) {
        return;
    }

    capture_exclusion_failed_ = true;
    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();
    set_page_health(
        HudPageState::Fatal,
        page_provider_,
        static_cast<std::uint16_t>(
            kCaptureExclusionLostExitCode));
    OutputDebugStringW(
        L"[ChatView HUD] Capture exclusion was lost; "
        L"hiding and terminating the HUD\n");
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
        KillTimer(window_, kStatusTimerId);
        KillTimer(window_, kNavigationRetryTimerId);
        KillTimer(window_, kCaptureSafetyTimerId);
    }
    PostQuitMessage(kCaptureExclusionLostExitCode);
}

bool HudWindow::apply_capture_policy() noexcept
{
    if (window_ == nullptr || !webview_ready_ ||
        capture_exclusion_failed_) {
        return !capture_exclusion_failed_;
    }

    if (capture_risk_ || system_suppressed()) {
        page_connection_recovery_.reset();
        page_health_watchdog_.disarm();
        if (edit_mode_) {
            capture_and_persist_bounds();
            edit_mode_ = false;
            apply_window_mode();
            update_host_state();
        }
        ShowWindow(window_, SW_HIDE);
        return true;
    }

    if (!SetWindowDisplayAffinity(
            window_, WDA_EXCLUDEFROMCAPTURE)) {
        debug_windows_error(
            L"SetWindowDisplayAffinity(restore HUD)");
        fail_closed_capture_exclusion();
        return false;
    }
    if (!capture_exclusion_intact()) {
        fail_closed_capture_exclusion();
        return false;
    }

    if (!SetWindowPos(
            window_,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                SWP_SHOWWINDOW)) {
        debug_windows_error(L"SetWindowPos(show HUD)");
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            static_cast<std::uint16_t>(12U));
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(12);
        return false;
    }

    if (!capture_exclusion_intact()) {
        fail_closed_capture_exclusion();
        return false;
    }

    if (!page_health_watchdog_.armed() &&
        page_provider_ != HudProvider::Unknown &&
        page_state_ != HudPageState::SetupRequired &&
        page_state_ != HudPageState::Retrying &&
        page_state_ != HudPageState::NetworkOffline &&
        page_state_ != HudPageState::Fatal) {
        page_health_watchdog_.arm(GetTickCount64());
    }
    return true;
}

void HudWindow::update_host_state() noexcept
{
    if (!webview_ready_ || capture_exclusion_failed_) {
        return;
    }

    std::wstring status = transient_status_;
    std::wstring tone = transient_tone_;
    if (status.empty() && !navigation_status_.empty()) {
        status = navigation_status_;
        tone = navigation_tone_;
    }
    if (status.empty()) {
        if (streaming_ && recording_) {
            status = L"LIVE  •  REC";
            tone = L"#ff3b30";
        } else if (streaming_) {
            status = L"LIVE";
            tone = L"#ff3b30";
        } else if (recording_) {
            status = L"REC";
            tone = L"#ff9500";
        }
    }

    webview_.set_host_state(edit_mode_, status, tone);
}

void HudWindow::set_transient_status(
    std::wstring text,
    std::wstring tone,
    UINT duration_ms)
{
    transient_status_ = std::move(text);
    transient_tone_ = std::move(tone);
    if (window_ != nullptr) {
        KillTimer(window_, kStatusTimerId);
        if (SetTimer(
                window_,
                kStatusTimerId,
                duration_ms,
                nullptr) == 0U) {
            debug_windows_error(L"SetTimer(status)");
        }
    }
    update_host_state();
}

void HudWindow::clear_transient_status() noexcept
{
    if (window_ != nullptr) {
        KillTimer(window_, kStatusTimerId);
    }
    transient_status_.clear();
    transient_tone_ = L"#aeb0b2";
    update_host_state();
}

void HudWindow::capture_and_persist_bounds() noexcept
{
    HudPlacement captured = capture_hud_placement(window_);
    if (!captured.valid) {
        return;
    }

    placement_ = std::move(captured);
    if (!save_hud_placement(placement_)) {
        OutputDebugStringW(
            L"[ChatView HUD] Failed to persist HUD bounds\n");
    }
}

void HudWindow::restore_saved_bounds() noexcept
{
    if (window_ == nullptr || capture_exclusion_failed_) {
        return;
    }

    const RECT bounds = resolve_hud_bounds(
        placement_, kDefaultMarginDip);
    if (!SetWindowPos(
            window_,
            HWND_TOPMOST,
            bounds.left,
            bounds.top,
            bounds.right - bounds.left,
            bounds.bottom - bounds.top,
            SWP_NOACTIVATE)) {
        debug_windows_error(L"SetWindowPos(restore bounds)");
        set_page_health(
            HudPageState::Fatal,
            page_provider_,
            static_cast<std::uint16_t>(18U));
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(18);
        return;
    }
    if (!capture_exclusion_intact()) {
        fail_closed_capture_exclusion();
        return;
    }
    webview_.resize();
}

void HudWindow::set_page_health(
    HudPageState state,
    HudProvider provider,
    std::uint16_t detail_code_value) noexcept
{
    page_state_ = state;
    page_provider_ = provider;
    page_detail_code_ = detail_code_value;
}

HudHealthSnapshot HudWindow::page_health() const noexcept
{
    return HudHealthSnapshot{
        page_state_, page_provider_, page_detail_code_};
}

UINT HudWindow::dpi() const noexcept
{
    if (window_ == nullptr) {
        return 96U;
    }

    using GetDpiForWindowFunction = UINT(WINAPI *)(HWND);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto get_dpi_for_window =
        reinterpret_cast<GetDpiForWindowFunction>(
            GetProcAddress(user32, "GetDpiForWindow"));
    if (get_dpi_for_window == nullptr) {
        return 96U;
    }

    const UINT value = get_dpi_for_window(window_);
    return value == 0U ? 96U : value;
}

} // namespace chatview
