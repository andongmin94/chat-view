// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Windows.h>
#include <commctrl.h>

namespace chatview {
// Reference primitives painted on native controls: keep their input and accessibility.
class NativePanelTheme final {
public:
    static constexpr COLORREF ink = RGB(0, 0, 0);
    static constexpr COLORREF main = RGB(39, 40, 43);
    static constexpr COLORREF background = RGB(244, 245, 247);
    NativePanelTheme() noexcept : background_(CreateSolidBrush(background)), paper_(CreateSolidBrush(RGB(255, 255, 255))), main_(CreateSolidBrush(main)) {}
    ~NativePanelTheme() { DeleteObject(body_); DeleteObject(bold_); DeleteObject(card_); DeleteObject(background_); DeleteObject(paper_); DeleteObject(main_); }
    NativePanelTheme(const NativePanelTheme &) = delete;
    NativePanelTheme &operator=(const NativePanelTheme &) = delete;
    void initialize(HWND window) noexcept {
        static const PrivateFonts fonts;
        (void)fonts;
        const int dpi = static_cast<int>(GetDpiForWindow(window));
        DeleteObject(body_); DeleteObject(bold_); DeleteObject(card_);
        body_ = font(14, dpi, FW_MEDIUM, L"Pretendard Medium");
        bold_ = font(14, dpi, FW_BOLD, L"Pretendard");
        card_ = font(16, dpi, FW_BOLD, L"Pretendard");
    }
    [[nodiscard]] HBRUSH background_brush() const noexcept { return background_; }
    [[nodiscard]] HFONT body_font() const noexcept { return body_; }
    [[nodiscard]] HFONT bold_font() const noexcept { return bold_; }
    [[nodiscard]] HFONT card_font() const noexcept { return card_; }
    void apply(HWND control, bool primary = false) const noexcept {
        wchar_t kind[32]{}; GetClassNameW(control, kind, 32);
        const auto type = GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK;
        const bool button = lstrcmpiW(kind, L"Button") == 0;
        const bool push = button && (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON);
        const bool group = button && type == BS_GROUPBOX;
        const bool check = button && (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(group ? card_ : body_), TRUE);
        if (push) SetWindowSubclass(control, button_procedure, 0x43564e, primary ? 1U : 0U);
        else if (group) SetWindowSubclass(control, group_procedure, 0x43564e, 0);
        else if (check) SetWindowSubclass(control, checkbox_procedure, 0x43564e, 0);
        else if (lstrcmpiW(kind, L"ComboBox") == 0 &&
            (GetWindowLongPtrW(control, GWL_STYLE) & CBS_DROPDOWNLIST) == CBS_DROPDOWNLIST)
            SetWindowSubclass(control, combo_procedure, 0x43564e, 0);
        else if (lstrcmpiW(kind, L"Edit") == 0) {
            SetWindowLongPtrW(control, GWL_EXSTYLE, GetWindowLongPtrW(control, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
            SetWindowLongPtrW(control, GWL_STYLE, GetWindowLongPtrW(control, GWL_STYLE) & ~WS_BORDER);
            SetWindowSubclass(control, edit_procedure, 0x43564e, 0);
            SetWindowPos(control, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
    [[nodiscard]] LRESULT color_control(HDC dc, bool edit = false) const noexcept {
        SetTextColor(dc, ink); SetBkColor(dc, edit ? RGB(255, 255, 255) : background);
        SetBkMode(dc, edit ? OPAQUE : TRANSPARENT);
        return reinterpret_cast<LRESULT>(edit ? paper_ : background_);
    }
    static void paint_card(HDC dc, RECT bounds, UINT dpi) noexcept {
        const int saved = SaveDC(dc);
        SelectObject(dc, GetStockObject(NULL_PEN)); SelectObject(dc, GetStockObject(DC_BRUSH));
        const int border = MulDiv(2, static_cast<int>(dpi), 96), shadow = MulDiv(4, static_cast<int>(dpi), 96);
        const int corner = MulDiv(10, static_cast<int>(dpi), 96);
        bounds.right -= shadow; bounds.bottom -= shadow;
        RECT offset = bounds; OffsetRect(&offset, shadow, shadow);
        SetDCBrushColor(dc, ink); RoundRect(dc, offset.left, offset.top, offset.right, offset.bottom, corner, corner);
        frame(dc, bounds, background, ink, border, corner);
        RestoreDC(dc, saved);
    }
    bool handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam, LRESULT &result) const noexcept {
        if (message == WM_DRAWITEM && reinterpret_cast<const DRAWITEMSTRUCT *>(lparam)->CtlType == ODT_COMBOBOX) {
            paint_combo_item(*reinterpret_cast<const DRAWITEMSTRUCT *>(lparam));
            result = TRUE; return true;
        }
        if (message == WM_ERASEBKGND) {
            RECT bounds{}; GetClientRect(window, &bounds);
            FillRect(reinterpret_cast<HDC>(wparam), &bounds, background_);
            result = 1; return true;
        }
        if (message == WM_CTLCOLORLISTBOX) {
            const HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, background); SetBkColor(dc, main); SetBkMode(dc, OPAQUE);
            SelectObject(dc, body_);
            result = reinterpret_cast<LRESULT>(main_); return true;
        }
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN || message == WM_CTLCOLOREDIT) {
            result = color_control(reinterpret_cast<HDC>(wparam), message == WM_CTLCOLOREDIT);
            return true;
        }
        return false;
    }
private:
    struct PrivateFonts {
        HANDLE medium = load(L"CHATVIEW_PRETENDARD_MEDIUM"), bold = load(L"CHATVIEW_PRETENDARD_BOLD");
        static HANDLE load(const wchar_t *name) noexcept {
            const HMODULE module = GetModuleHandleW(nullptr);
            const HRSRC resource = FindResourceW(module, name, RT_RCDATA);
            if (!resource) return nullptr;
            const HGLOBAL data = LoadResource(module, resource);
            DWORD count = 0;
            return AddFontMemResourceEx(LockResource(data), SizeofResource(module, resource), nullptr, &count);
        }
        ~PrivateFonts() { if (medium) RemoveFontMemResourceEx(medium); if (bold) RemoveFontMemResourceEx(bold); }
    };
    static HFONT font(int size, int dpi, int weight, const wchar_t *family) noexcept {
        return CreateFontW(-MulDiv(size, dpi, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, family);
    }
    static void frame(HDC dc, RECT bounds, COLORREF fill, COLORREF line, int border, int corner) noexcept {
        SetDCBrushColor(dc, line); RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, corner, corner);
        InflateRect(&bounds, -border, -border);
        SetDCBrushColor(dc, fill);
        RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, corner - border * 2, corner - border * 2);
    }
    static COLORREF half(COLORREF color) noexcept {
        return RGB((GetRValue(color) + (background & 0xffU)) / 2,
            (GetGValue(color) + ((background >> 8U) & 0xffU)) / 2,
            (GetBValue(color) + ((background >> 16U) & 0xffU)) / 2);
    }
    static void paint_checkbox(HWND window, HDC dc) noexcept {
        const int saved = SaveDC(dc), dpi = static_cast<int>(GetDpiForWindow(window));
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        RECT bounds{}; GetClientRect(window, &bounds);
        SelectObject(dc, GetStockObject(NULL_PEN)); SelectObject(dc, GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc, background); FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const bool enabled = IsWindowEnabled(window) != FALSE;
        const auto checked = SendMessageW(window, BM_GETCHECK, 0, 0);
        const int top = (bounds.bottom - scale(16)) / 2;
        RECT box{scale(1), top, scale(17), top + scale(16)};
        const COLORREF fill = checked == BST_UNCHECKED ? RGB(255, 255, 255) : main;
        frame(dc, box, enabled ? fill : half(fill), enabled ? ink : half(ink), scale(2), scale(10));
        if (checked != BST_UNCHECKED) {
            const HPEN pen = CreatePen(PS_SOLID, scale(2), enabled ? background : half(background));
            const HGDIOBJ previous = SelectObject(dc, pen);
            const int left = box.left + scale(1), middle = top + scale(8);
            if (checked == BST_INDETERMINATE) {
                MoveToEx(dc, left + scale(3), middle, nullptr); LineTo(dc, left + scale(11), middle);
            } else {
                MoveToEx(dc, left + scale(3), top + scale(8), nullptr);
                LineTo(dc, left + scale(5), top + scale(11)); LineTo(dc, left + scale(12), top + scale(4));
            }
            SelectObject(dc, previous); DeleteObject(pen);
        }
        const auto ui = SendMessageW(window, WM_QUERYUISTATE, 0, 0);
        if (GetFocus() == window && (ui & UISF_HIDEFOCUS) == 0) {
            const HPEN pen = CreatePen(PS_SOLID, scale(1), ink);
            const HGDIOBJ previous = SelectObject(dc, pen); SelectObject(dc, GetStockObject(NULL_BRUSH));
            InflateRect(&box, scale(1), scale(1));
            RoundRect(dc, box.left, box.top, box.right, box.bottom, scale(12), scale(12));
            SelectObject(dc, previous); DeleteObject(pen);
        }
        SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
        SetTextColor(dc, enabled ? ink : half(ink)); SetBkMode(dc, TRANSPARENT);
        wchar_t caption[256]{}; GetWindowTextW(window, caption, 256);
        bounds.left += scale(25);
        DrawTextW(dc, caption, -1, &bounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
            ((ui & UISF_HIDEACCEL) != 0 ? DT_HIDEPREFIX : 0));
        RestoreDC(dc, saved);
    }
    static void paint_combo(HWND window, HDC dc) noexcept {
        const int saved = SaveDC(dc), dpi = static_cast<int>(GetDpiForWindow(window));
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        RECT bounds{}; GetClientRect(window, &bounds);
        SelectObject(dc, GetStockObject(NULL_PEN)); SelectObject(dc, GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc, background); FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const bool enabled = IsWindowEnabled(window) != FALSE;
        frame(dc, bounds, enabled ? main : half(main), enabled ? ink : half(ink), scale(2), scale(10));
        SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
        SetTextColor(dc, background); SetBkMode(dc, TRANSPARENT);
        RECT label = bounds; label.left += scale(14); label.right -= scale(38);
        wchar_t caption[512]{}; GetWindowTextW(window, caption, 512);
        DrawTextW(dc, caption, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        // The reference chevron occupies 16 px with a two-pixel stroke at 70% opacity.
        const COLORREF chevron = enabled ? RGB(183, 184, 186) : RGB(214, 215, 217);
        const HPEN pen = CreatePen(PS_SOLID, scale(2), chevron);
        const HGDIOBJ previous = SelectObject(dc, pen);
        const int left = bounds.right - scale(30), top = (bounds.bottom - scale(16)) / 2;
        MoveToEx(dc, left + scale(4), top + scale(6), nullptr);
        LineTo(dc, left + scale(8), top + scale(10)); LineTo(dc, left + scale(12), top + scale(6));
        SelectObject(dc, previous); DeleteObject(pen);
        RestoreDC(dc, saved);
    }
    static void paint_combo_item(const DRAWITEMSTRUCT &item) noexcept {
        if ((item.itemState & ODS_COMBOBOXEDIT) != 0U) { paint_combo(item.hwndItem, item.hDC); return; }
        const int saved = SaveDC(item.hDC), dpi = static_cast<int>(GetDpiForWindow(item.hwndItem));
        const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        SelectObject(item.hDC, GetStockObject(NULL_PEN)); SelectObject(item.hDC, GetStockObject(DC_BRUSH));
        SetDCBrushColor(item.hDC, main); FillRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if ((item.itemState & (ODS_SELECTED | ODS_FOCUS)) != 0U)
            frame(item.hDC, item.rcItem, main, background, scale(2), scale(10));
        if (item.itemID != static_cast<UINT>(-1)) {
            wchar_t caption[1400]{};
            const auto length = SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, item.itemID, 0);
            // Source labels include the existing 1024-wchar process path and 257-wchar window title.
            // CB_GETLBTEXT has no capacity argument, so check the stored length before copying.
            if (length >= 0 && length < static_cast<LRESULT>(sizeof(caption) / sizeof(caption[0])))
                SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID, reinterpret_cast<LPARAM>(caption));
            SelectObject(item.hDC, reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
            SetTextColor(item.hDC, background); SetBkMode(item.hDC, TRANSPARENT);
            RECT label = item.rcItem; label.left += scale(10); label.right -= scale(34);
            DrawTextW(item.hDC, caption, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if ((item.itemState & ODS_SELECTED) != 0U) {
                const HPEN pen = CreatePen(PS_SOLID, scale(2), background);
                const HGDIOBJ previous = SelectObject(item.hDC, pen);
                const int left = item.rcItem.right - scale(24), top = (item.rcItem.top + item.rcItem.bottom - scale(16)) / 2;
                MoveToEx(item.hDC, left + scale(3), top + scale(8), nullptr);
                LineTo(item.hDC, left + scale(6), top + scale(11)); LineTo(item.hDC, left + scale(13), top + scale(4));
                SelectObject(item.hDC, previous); DeleteObject(pen);
            }
        }
        RestoreDC(item.hDC, saved);
    }
    static LRESULT CALLBACK checkbox_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                               UINT_PTR id, DWORD_PTR) noexcept {
        if (message == WM_PAINT) { PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window, &paint);
            paint_checkbox(window, dc); EndPaint(window, &paint); return 0; }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PRINTCLIENT) { paint_checkbox(window, reinterpret_cast<HDC>(wparam)); return 0; }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, checkbox_procedure, id);
        const auto result = DefSubclassProc(window, message, wparam, lparam);
        if (message == BM_SETCHECK || message == BM_SETSTATE || message == WM_ENABLE || message == WM_SETFOCUS ||
            message == WM_KILLFOCUS || message == WM_SETTEXT || message == WM_UPDATEUISTATE)
            InvalidateRect(window, nullptr, TRUE);
        return result;
    }
    static LRESULT CALLBACK combo_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                            UINT_PTR id, DWORD_PTR) noexcept {
        if (message == WM_PAINT) { PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window, &paint);
            paint_combo(window, dc); EndPaint(window, &paint); return 0; }
        if (message == WM_ERASEBKGND || message == WM_NCPAINT) return 1;
        if (message == WM_PRINTCLIENT) { paint_combo(window, reinterpret_cast<HDC>(wparam)); return 0; }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, combo_procedure, id);
        const auto result = DefSubclassProc(window, message, wparam, lparam);
        if (message == CB_SETCURSEL || message == CB_RESETCONTENT || message == CB_DELETESTRING ||
            message == WM_ENABLE || message == WM_SETFOCUS || message == WM_KILLFOCUS ||
            message == WM_SETTEXT || message == WM_UPDATEUISTATE || message == WM_THEMECHANGED)
            InvalidateRect(window, nullptr, TRUE);
        return result;
    }
    static void paint_button(HWND window, HDC dc, DWORD_PTR flags) noexcept {
        const int saved = SaveDC(dc);
        RECT face{}; GetClientRect(window, &face);
        SetDCBrushColor(dc, background); FillRect(dc, &face, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const int dpi = static_cast<int>(GetDpiForWindow(window));
        const int border = MulDiv(2, dpi, 96), shadow = MulDiv(4, dpi, 96), corner = MulDiv(10, dpi, 96);
        const auto state = SendMessageW(window, BM_GETSTATE, 0, 0);
        const bool enabled = IsWindowEnabled(window) != FALSE, primary = (flags & 1U) != 0U;
        const bool pressed = enabled && (state & BST_PUSHED) != 0;
        const int move = pressed ? shadow : enabled && (flags & 2U) != 0U ? MulDiv(3, dpi, 96) : 0;
        face.right -= shadow; face.bottom -= shadow; OffsetRect(&face, move, move);
        SelectObject(dc, GetStockObject(NULL_PEN)); SelectObject(dc, GetStockObject(DC_BRUSH));
        if (!pressed) {
            RECT offset = face; OffsetRect(&offset, shadow - move, shadow - move);
            SetDCBrushColor(dc, enabled ? ink : half(ink));
            RoundRect(dc, offset.left, offset.top, offset.right, offset.bottom, corner, corner);
        }
        const COLORREF fill = primary ? main : background;
        frame(dc, face, enabled ? fill : half(fill), enabled ? ink : half(ink), border, corner);
        SetBkMode(dc, TRANSPARENT);
        const COLORREF text = primary ? background : ink;
        SetTextColor(dc, enabled ? text : half(text));
        SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
        wchar_t caption[256]{}; GetWindowTextW(window, caption, 256);
        RECT label = face; InflateRect(&label, -MulDiv(16, dpi, 96), -border);
        DrawTextW(dc, caption, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if ((state & BST_FOCUS) != 0 && (SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0) {
            InflateRect(&face, -border * 2, -border * 2); DrawFocusRect(dc, &face);
        }
        RestoreDC(dc, saved);
    }
    static LRESULT CALLBACK button_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                             UINT_PTR id, DWORD_PTR flags) noexcept {
        if (message == WM_PAINT) { PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window, &paint);
            paint_button(window, dc, flags); EndPaint(window, &paint); return 0; }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PRINTCLIENT) { paint_button(window, reinterpret_cast<HDC>(wparam), flags); return 0; }
        if (message == WM_MOUSEMOVE && (flags & 2U) == 0U) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
            SetWindowSubclass(window, button_procedure, id, flags | 2U); InvalidateRect(window, nullptr, TRUE);
        }
        if (message == WM_MOUSELEAVE) { SetWindowSubclass(window, button_procedure, id, flags & ~2U); InvalidateRect(window, nullptr, TRUE); }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, button_procedure, id);
        const auto result = DefSubclassProc(window, message, wparam, lparam);
        if (message == BM_SETSTATE || message == WM_ENABLE || message == WM_SETFOCUS ||
            message == WM_KILLFOCUS || message == WM_SETTEXT || message == WM_UPDATEUISTATE)
            InvalidateRect(window, nullptr, TRUE);
        return result;
    }
    static LRESULT CALLBACK group_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                            UINT_PTR id, DWORD_PTR) noexcept {
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window, &paint); RECT bounds{}; GetClientRect(window, &bounds);
            paint_card(dc, bounds, GetDpiForWindow(window));
            const int saved = SaveDC(dc), pad = MulDiv(24, static_cast<int>(GetDpiForWindow(window)), 96);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, ink);
            SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
            bounds.left += pad; bounds.top += pad; bounds.right -= pad; bounds.bottom = bounds.top + pad;
            wchar_t caption[256]{}; GetWindowTextW(window, caption, 256);
            DrawTextW(dc, caption, -1, &bounds, DT_LEFT | DT_SINGLELINE); RestoreDC(dc, saved);
            EndPaint(window, &paint); return 0;
        }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, group_procedure, id);
        return DefSubclassProc(window, message, wparam, lparam);
    }
    static LRESULT CALLBACK edit_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                           UINT_PTR id, DWORD_PTR) noexcept {
        if (message == WM_NCCALCSIZE) {
            const auto result = DefSubclassProc(window, message, wparam, lparam);
            RECT &bounds = wparam ? reinterpret_cast<NCCALCSIZE_PARAMS *>(lparam)->rgrc[0] : *reinterpret_cast<RECT *>(lparam);
            const int dpi = static_cast<int>(GetDpiForWindow(window));
            InflateRect(&bounds, -MulDiv(14, dpi, 96), -MulDiv(10, dpi, 96));
            return result;
        }
        if (message == WM_NCPAINT) {
            const HDC dc = GetWindowDC(window); const int saved = SaveDC(dc);
            RECT bounds{}, client{}; GetWindowRect(window, &bounds); GetClientRect(window, &client);
            POINT origin{}; ClientToScreen(window, &origin);
            ExcludeClipRect(dc, origin.x - bounds.left, origin.y - bounds.top,
                origin.x - bounds.left + client.right, origin.y - bounds.top + client.bottom);
            OffsetRect(&bounds, -bounds.left, -bounds.top);
            SelectObject(dc, GetStockObject(NULL_PEN)); SelectObject(dc, GetStockObject(DC_BRUSH));
            SetDCBrushColor(dc, background); FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            const int dpi = static_cast<int>(GetDpiForWindow(window));
            frame(dc, bounds, RGB(255, 255, 255), ink, MulDiv(2, dpi, 96), MulDiv(10, dpi, 96));
            RestoreDC(dc, saved); ReleaseDC(window, dc); return 0;
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, edit_procedure, id);
        const auto result = DefSubclassProc(window, message, wparam, lparam);
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE)
            RedrawWindow(window, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        return result;
    }
    HFONT body_ = nullptr, bold_ = nullptr, card_ = nullptr;
    HBRUSH background_ = nullptr, paper_ = nullptr, main_ = nullptr;
};
}
