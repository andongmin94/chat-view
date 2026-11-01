// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Windows.h>
#include <commctrl.h>

namespace chatview {
// Paint the existing Win32 controls; their input, focus and accessibility stay native.
class NativePanelTheme final {
public:
    static constexpr COLORREF ink = RGB(39, 40, 43);
    static constexpr COLORREF background = RGB(244, 245, 247);
    NativePanelTheme() noexcept : background_(CreateSolidBrush(background)), paper_(CreateSolidBrush(RGB(255, 255, 255))) {}
    ~NativePanelTheme() { DeleteObject(body_); DeleteObject(bold_); DeleteObject(background_); DeleteObject(paper_); }
    NativePanelTheme(const NativePanelTheme &) = delete;
    NativePanelTheme &operator=(const NativePanelTheme &) = delete;
    void initialize(HWND window) noexcept {
        const int height = -MulDiv(14, static_cast<int>(GetDpiForWindow(window)), 96);
        DeleteObject(body_); DeleteObject(bold_);
        body_ = CreateFontW(height, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        bold_ = CreateFontW(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    [[nodiscard]] HBRUSH background_brush() const noexcept { return background_; }
    [[nodiscard]] HFONT body_font() const noexcept { return body_; }
    [[nodiscard]] HFONT bold_font() const noexcept { return bold_; }
    void apply(HWND control, bool primary = false) const noexcept {
        wchar_t kind[32]{}; GetClassNameW(control, kind, 32);
        const auto type = GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK;
        const bool push = lstrcmpW(kind, L"Button") == 0 && (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(push ? bold_ : body_), TRUE);
        if (push) SetWindowSubclass(control, button_procedure, 0x43564e, primary ? 1U : 0U);
    }
    [[nodiscard]] LRESULT color_control(HDC dc, bool edit = false) const noexcept {
        SetTextColor(dc, ink); SetBkColor(dc, edit ? RGB(255, 255, 255) : background);
        SetBkMode(dc, edit ? OPAQUE : TRANSPARENT);
        return reinterpret_cast<LRESULT>(edit ? paper_ : background_);
    }
    bool handle_message(HWND window, UINT message, WPARAM wparam, LPARAM, LRESULT &result) const noexcept {
        if (message == WM_ERASEBKGND) {
            RECT bounds{}; GetClientRect(window, &bounds);
            FillRect(reinterpret_cast<HDC>(wparam), &bounds, background_);
            result = 1; return true;
        }
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN ||
            message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
            result = color_control(reinterpret_cast<HDC>(wparam),
                message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX);
            return true;
        }
        return false;
    }
private:
    static void paint_button(HWND window, HDC dc, bool primary) noexcept {
        RECT bounds{}; GetClientRect(window, &bounds);
        const int unit = MulDiv(2, static_cast<int>(GetDpiForWindow(window)), 96);
        const int shadow = unit * 2;
        const auto state = SendMessageW(window, BM_GETSTATE, 0, 0);
        const bool pressed = (state & BST_PUSHED) != 0;
        const bool enabled = IsWindowEnabled(window) != FALSE;
        SetDCBrushColor(dc, background); FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        RECT face = bounds; face.right -= shadow; face.bottom -= shadow;
        RECT offset = face; OffsetRect(&offset, shadow, shadow);
        SetDCBrushColor(dc, ink); FillRect(dc, &offset, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if (pressed) OffsetRect(&face, shadow, shadow);
        const HPEN pen = CreatePen(PS_SOLID, unit, ink);
        const auto old_pen = SelectObject(dc, pen);
        const auto old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc, primary && enabled ? ink : enabled ? RGB(255, 255, 255) : background);
        RoundRect(dc, face.left + unit / 2, face.top + unit / 2, face.right - unit / 2, face.bottom - unit / 2,
            unit * 3, unit * 3);
        SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(pen);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, !enabled ? RGB(112, 113, 118) : primary ? RGB(255, 255, 255) : ink);
        const auto old_font = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
        wchar_t caption[256]{}; GetWindowTextW(window, caption, 256);
        RECT label = face; InflateRect(&label, -unit * 2, -unit);
        DrawTextW(dc, caption, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if ((state & BST_FOCUS) != 0) { InflateRect(&face, -unit * 2, -unit * 2); DrawFocusRect(dc, &face); }
        SelectObject(dc, old_font);
    }
    static LRESULT CALLBACK button_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                             UINT_PTR id, DWORD_PTR primary) noexcept {
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window, &paint);
            paint_button(window, dc, primary != 0U); EndPaint(window, &paint); return 0;
        }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PRINTCLIENT) { paint_button(window, reinterpret_cast<HDC>(wparam), primary != 0U); return 0; }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, button_procedure, id);
        const auto result = DefSubclassProc(window, message, wparam, lparam);
        if (message == BM_SETSTATE || message == WM_ENABLE || message == WM_SETFOCUS ||
            message == WM_KILLFOCUS || message == WM_SETTEXT || message == WM_UPDATEUISTATE)
            InvalidateRect(window, nullptr, TRUE);
        return result;
    }
    HFONT body_ = nullptr, bold_ = nullptr;
    HBRUSH background_ = nullptr, paper_ = nullptr;
};
}
