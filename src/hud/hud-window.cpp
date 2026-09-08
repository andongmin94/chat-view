// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/hud-window.hpp"

#include <Windows.h>
#include <gdiplus.h>
#include <strsafe.h>

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cwchar>

namespace chatview {
namespace {

constexpr wchar_t kWindowClassName[] = L"ChatViewObsHudWindow";
constexpr UINT_PTR kHideTimerId = 1U;
constexpr UINT kReadyDurationMs = 2200U;
constexpr UINT kOfflineDurationMs = 1200U;

#ifndef WDA_EXCLUDEFROMCAPTURE
constexpr DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
#endif

void debug_windows_error(const wchar_t *operation)
{
    const DWORD error = GetLastError();
    wchar_t message[256]{};
    const HRESULT result = StringCchPrintfW(
        message,
        std::size(message),
        L"[ChatView HUD] %s failed with error %lu\n",
        operation,
        error);
    if (SUCCEEDED(result)) {
        OutputDebugStringW(message);
    }
}

} // namespace

HudWindow::~HudWindow()
{
    destroy();
}

bool HudWindow::create(HINSTANCE instance)
{
    instance_ = instance;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &HudWindow::window_proc;
    window_class.hInstance = instance_;
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (RegisterClassExW(&window_class) == 0U && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        debug_windows_error(L"RegisterClassExW");
        return false;
    }

    constexpr DWORD extended_style =
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;

    window_ = CreateWindowExW(
        extended_style,
        kWindowClassName,
        L"ChatView HUD",
        WS_POPUP,
        0,
        0,
        1,
        1,
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

    ShowWindow(window_, SW_HIDE);
    return true;
}

void HudWindow::destroy() noexcept
{
    cancel_hide_timer();

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
    render(DisplayMode::Ready);
    arm_hide_timer(kReadyDurationMs);
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

    const bool streaming = has_flag(snapshot, SharedStateStreaming);
    const bool recording = has_flag(snapshot, SharedStateRecording);

    if (streaming && recording) {
        has_seen_active_state_ = true;
        cancel_hide_timer();
        render(DisplayMode::LiveAndRecording);
        return;
    }

    if (streaming) {
        has_seen_active_state_ = true;
        cancel_hide_timer();
        render(DisplayMode::Live);
        return;
    }

    if (recording) {
        has_seen_active_state_ = true;
        cancel_hide_timer();
        render(DisplayMode::Recording);
        return;
    }

    if (has_seen_active_state_) {
        has_seen_active_state_ = false;
        render(DisplayMode::Offline);
        arm_hide_timer(kOfflineDurationMs);
    }
}

LRESULT CALLBACK HudWindow::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    HudWindow *self = reinterpret_cast<HudWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
        self = static_cast<HudWindow *>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr) {
        return self->handle_message(message, wparam, lparam);
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT HudWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_TIMER:
        if (wparam == kHideTimerId) {
            cancel_hide_timer();
            hide();
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        if (mode_ != DisplayMode::Hidden) {
            render(mode_);
        }
        return 0;
    case WM_CLOSE:
        hide();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window_, message, wparam, lparam);
}

void HudWindow::render(DisplayMode mode)
{
    if (window_ == nullptr || mode == DisplayMode::Hidden) {
        hide();
        return;
    }

    mode_ = mode;

    const float scale = static_cast<float>(dpi()) / 96.0F;
    const int width = std::max(1, static_cast<int>(std::lround(340.0F * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(76.0F * scale)));
    const int margin = std::max(1, static_cast<int>(std::lround(24.0F * scale)));

    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    const HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(monitor, &monitor_info)) {
        debug_windows_error(L"GetMonitorInfoW");
        return;
    }

    const POINT destination{
        monitor_info.rcWork.right - width - margin,
        monitor_info.rcWork.top + margin,
    };
    const SIZE size{width, height};
    const POINT source{0, 0};

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void *pixels = nullptr;
    HBITMAP bitmap =
        CreateDIBSection(nullptr, &bitmap_info, DIB_RGB_COLORS, &pixels, nullptr, 0U);
    if (bitmap == nullptr || pixels == nullptr) {
        debug_windows_error(L"CreateDIBSection");
        return;
    }

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        debug_windows_error(L"GetDC");
        DeleteObject(bitmap);
        return;
    }

    HDC memory_dc = CreateCompatibleDC(screen_dc);
    if (memory_dc == nullptr) {
        debug_windows_error(L"CreateCompatibleDC");
        ReleaseDC(nullptr, screen_dc);
        DeleteObject(bitmap);
        return;
    }

    HGDIOBJ previous_bitmap = SelectObject(memory_dc, bitmap);
    if (previous_bitmap == nullptr || previous_bitmap == HGDI_ERROR) {
        debug_windows_error(L"SelectObject");
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        DeleteObject(bitmap);
        return;
    }

    {
        Gdiplus::Bitmap surface(
            width,
            height,
            width * 4,
            PixelFormat32bppPARGB,
            static_cast<BYTE *>(pixels));
        Gdiplus::Graphics graphics(&surface);
        graphics.Clear(Gdiplus::Color(0U, 0U, 0U, 0U));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        Gdiplus::Color accent(255U, 174U, 174U, 178U);
        switch (mode) {
        case DisplayMode::Live:
        case DisplayMode::LiveAndRecording:
            accent = Gdiplus::Color(255U, 255U, 59U, 48U);
            break;
        case DisplayMode::Recording:
            accent = Gdiplus::Color(255U, 255U, 149U, 0U);
            break;
        case DisplayMode::Ready:
            accent = Gdiplus::Color(255U, 90U, 200U, 250U);
            break;
        case DisplayMode::Offline:
        case DisplayMode::Hidden:
            break;
        }

        const float dot_size = 12.0F * scale;
        const float dot_x = 18.0F * scale;
        const float dot_y = (static_cast<float>(height) - dot_size) / 2.0F;

        Gdiplus::SolidBrush accent_brush(accent);
        graphics.FillEllipse(&accent_brush, dot_x, dot_y, dot_size, dot_size);

        const wchar_t *label = label_for(mode);
        const INT label_length = static_cast<INT>(wcslen(label));
        Gdiplus::Font font(
            L"Segoe UI",
            22.0F * scale,
            Gdiplus::FontStyleBold,
            Gdiplus::UnitPixel);
        Gdiplus::SolidBrush shadow_brush(Gdiplus::Color(190U, 0U, 0U, 0U));
        Gdiplus::SolidBrush text_brush(Gdiplus::Color(255U, 255U, 255U, 255U));

        const float text_x = 42.0F * scale;
        const float text_y = 19.0F * scale;
        graphics.DrawString(
            label,
            label_length,
            &font,
            Gdiplus::PointF(text_x + 2.0F * scale, text_y + 2.0F * scale),
            &shadow_brush);
        graphics.DrawString(
            label,
            label_length,
            &font,
            Gdiplus::PointF(text_x, text_y),
            &text_brush);
    }

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255U;
    blend.AlphaFormat = AC_SRC_ALPHA;

    if (!UpdateLayeredWindow(
            window_,
            screen_dc,
            &destination,
            &size,
            memory_dc,
            &source,
            0U,
            &blend,
            ULW_ALPHA)) {
        debug_windows_error(L"UpdateLayeredWindow");
    } else {
        SetWindowPos(
            window_,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    SelectObject(memory_dc, previous_bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    DeleteObject(bitmap);
}

void HudWindow::hide()
{
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
    }
    mode_ = DisplayMode::Hidden;
}

void HudWindow::arm_hide_timer(UINT milliseconds)
{
    cancel_hide_timer();
    if (window_ != nullptr) {
        SetTimer(window_, kHideTimerId, milliseconds, nullptr);
    }
}

void HudWindow::cancel_hide_timer()
{
    if (window_ != nullptr) {
        KillTimer(window_, kHideTimerId);
    }
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

    return get_dpi_for_window != nullptr ? get_dpi_for_window(window_) : 96U;
}

const wchar_t *HudWindow::label_for(DisplayMode mode) const noexcept
{
    switch (mode) {
    case DisplayMode::Ready:
        return L"CHATVIEW READY";
    case DisplayMode::Live:
        return L"LIVE";
    case DisplayMode::Recording:
        return L"REC";
    case DisplayMode::LiveAndRecording:
        return L"LIVE  \u2022  REC";
    case DisplayMode::Offline:
        return L"OFFLINE";
    case DisplayMode::Hidden:
    default:
        return L"";
    }
}

} // namespace chatview
