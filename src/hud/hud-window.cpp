// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/hud-window.hpp"

#include "common/chat-config.hpp"

#include <Windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace chatview {
namespace {

constexpr wchar_t kWindowClassName[] = L"ChatViewObsHudWindow";
constexpr UINT_PTR kStatusTimerId = 1U;
constexpr int kEditHotkeyId = 1;
constexpr UINT kReadyDurationMs = 2200U;
constexpr UINT kOfflineDurationMs = 1200U;
constexpr UINT kEditHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
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
    window_class.lpszClassName = kWindowClassName;
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
    const RECT bounds = resolve_hud_bounds(placement_, kDefaultMarginDip);

    constexpr DWORD extended_style =
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT |
        WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP;
    window_ = CreateWindowExW(
        extended_style,
        kWindowClassName,
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

    if (!SetWindowDisplayAffinity(window_, WDA_EXCLUDEFROMCAPTURE)) {
        debug_windows_error(L"SetWindowDisplayAffinity");
    }

    config_changed_message_ = RegisterWindowMessageW(kConfigChangedMessageName);
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

    ShowWindow(window_, SW_HIDE);
    return true;
}

void HudWindow::destroy() noexcept
{
    KillTimer(window_, kStatusTimerId);

    if (window_ != nullptr && edit_hotkey_registered_) {
        UnregisterHotKey(window_, kEditHotkeyId);
        edit_hotkey_registered_ = false;
    }

    webview_.close();
    ready_event_ = nullptr;
    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (instance_ != nullptr) {
        UnregisterClassW(kWindowClassName, instance_);
        instance_ = nullptr;
    }
}

void HudWindow::show_ready()
{
    set_transient_status(L"READY", L"#5ac8fa", kReadyDurationMs);
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
    const bool active = streaming_ || recording_;

    if (active) {
        clear_transient_status();
    } else if (was_active) {
        set_transient_status(L"OFFLINE", L"#aeb0b2", kOfflineDurationMs);
        return;
    }
    update_host_state();
}

LRESULT CALLBACK HudWindow::window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    HudWindow *self =
        reinterpret_cast<HudWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
        self = static_cast<HudWindow *>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }

    return self != nullptr
               ? self->handle_message(window, message, wparam, lparam)
               : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT HudWindow::handle_message(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (config_changed_message_ != 0U && message == config_changed_message_) {
        reload_chat_config();
        return 0L;
    }

    switch (message) {
    case kWebViewReadyMessage:
        webview_ready_ = true;
        reload_chat_config();
        update_host_state();
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        SetWindowPos(
            window_,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (ready_event_ != nullptr && !SetEvent(ready_event_)) {
            debug_windows_error(L"SetEvent(ready)");
        }
        return 0L;
    case kWebViewDocumentReadyMessage:
        update_host_state();
        return 0L;
    case kWebViewFailedMessage: {
        wchar_t detail[192]{};
        swprintf_s(
            detail,
            L"[ChatView HUD] WebView2 initialization failed (0x%08lX)\n",
            static_cast<unsigned long>(static_cast<std::uint32_t>(wparam)));
        OutputDebugStringW(detail);
        PostQuitMessage(2);
        return 0L;
    }
    case WM_TIMER:
        if (wparam == kStatusTimerId) {
            clear_transient_status();
            return 0L;
        }
        break;
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
        minmax->ptMinTrackSize.x = minimum.cx;
        minmax->ptMinTrackSize.y = minimum.cy;
        return 0L;
    }
    case WM_ENTERSIZEMOVE:
        return 0L;
    case WM_EXITSIZEMOVE:
        if (edit_mode_) {
            capture_and_persist_bounds();
        }
        return 0L;
    case WM_MOVE:
        webview_.notify_parent_position_changed();
        return 0L;
    case WM_SIZE:
        webview_.resize();
        return 0L;
    case WM_DPICHANGED: {
        const RECT *suggested = reinterpret_cast<const RECT *>(lparam);
        SetWindowPos(
            window_,
            nullptr,
            suggested->left,
            suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        webview_.resize();
        if (edit_mode_) {
            capture_and_persist_bounds();
        }
        return 0L;
    }
    case WM_DISPLAYCHANGE:
        restore_saved_bounds();
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
        if (edit_mode_ && webview_.forward_mouse_message(message, wparam, lparam)) {
            return 0L;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window_);
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

    const int border = std::max(6, MulDiv(kResizeBorderDip, static_cast<int>(dpi()), 96));
    const int header = std::max(28, MulDiv(kDragHeaderDip, static_cast<int>(dpi()), 96));
    const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
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
    if (window_ == nullptr || !webview_ready_) {
        return;
    }

    if (edit_mode_) {
        capture_and_persist_bounds();
    }
    edit_mode_ = !edit_mode_;
    apply_window_mode();
    update_host_state();

    if (edit_mode_) {
        ShowWindow(window_, SW_SHOW);
        SetForegroundWindow(window_);
        webview_.focus();
    }
}

void HudWindow::apply_window_mode() noexcept
{
    if (window_ == nullptr) {
        return;
    }

    LONG_PTR style = GetWindowLongPtrW(window_, GWL_STYLE);
    LONG_PTR extended_style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    if (edit_mode_) {
        style |= WS_THICKFRAME;
        extended_style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    } else {
        style &= ~static_cast<LONG_PTR>(WS_THICKFRAME);
        extended_style |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    }

    SetWindowLongPtrW(window_, GWL_STYLE, style);
    SetWindowLongPtrW(window_, GWL_EXSTYLE, extended_style);
    SetWindowPos(
        window_,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    webview_.resize();
}

void HudWindow::reload_chat_config() noexcept
{
    if (!webview_ready_) {
        return;
    }

    ChatConfig config;
    if (load_chat_config(config)) {
        if (!webview_.navigate(config.url)) {
            webview_.show_setup_page();
        }
    } else {
        webview_.show_setup_page();
    }
}

void HudWindow::update_host_state() noexcept
{
    if (!webview_ready_) {
        return;
    }

    std::wstring status = transient_status_;
    std::wstring tone = transient_tone_;
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
    std::wstring text, std::wstring tone, UINT duration_ms)
{
    transient_status_ = std::move(text);
    transient_tone_ = std::move(tone);
    KillTimer(window_, kStatusTimerId);
    if (window_ != nullptr) {
        SetTimer(window_, kStatusTimerId, duration_ms, nullptr);
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
        OutputDebugStringW(L"[ChatView HUD] Failed to persist HUD bounds\n");
    }
}

void HudWindow::restore_saved_bounds() noexcept
{
    if (window_ == nullptr) {
        return;
    }

    const RECT bounds = resolve_hud_bounds(placement_, kDefaultMarginDip);
    SetWindowPos(
        window_,
        HWND_TOPMOST,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        SWP_NOACTIVATE);
    webview_.resize();
}

UINT HudWindow::dpi() const noexcept
{
    if (window_ == nullptr) {
        return 96U;
    }

    using GetDpiForWindowFunction = UINT(WINAPI *)(HWND);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto get_dpi_for_window = reinterpret_cast<GetDpiForWindowFunction>(
        GetProcAddress(user32, "GetDpiForWindow"));
    if (get_dpi_for_window == nullptr) {
        return 96U;
    }

    const UINT value = get_dpi_for_window(window_);
    return value == 0U ? 96U : value;
}

} // namespace chatview
