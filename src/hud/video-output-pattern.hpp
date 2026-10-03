// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud/video-output-check.hpp"
#include <Windows.h>
#include <cwchar>

namespace chatview {
// Paint only synthetic pixels on the independent cover HWND, never on the
// DXGI flip surface. No window title, private HUD, account or token is used.
inline bool paint_video_output_pattern(HDC dc, RECT area, std::uint32_t identifier, unsigned seconds) noexcept
{
    const int width = area.right - area.left, height = area.bottom - area.top;
    if (!dc || width < 160 || height < 120 || width > 4096 || height > 4096) return false;
    const int saved = SaveDC(dc);
    if (!saved) return false;
    const auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    const auto fill = [&](RECT rect, COLORREF color) {
        return SetDCBrushColor(dc, color) != CLR_INVALID && FillRect(dc, &rect, brush) != 0;
    };
    bool ok = fill(area, RGB(0, 0, 0));
    // A continuous inset border makes cropping distinguishable from black bars.
    const int x = area.left, y = area.top;
    const int margin = width / 40, edge = height / 60 > 1 ? height / 60 : 2;
    ok = fill({x + margin, y + edge, x + width - margin, y + edge * 2}, RGB(255, 255, 255)) && ok;
    ok = fill({x + margin, y + height - edge * 2, x + width - margin, y + height - edge}, RGB(255, 255, 255)) && ok;
    ok = fill({x + margin, y + edge, x + margin + edge, y + height - edge}, RGB(255, 255, 255)) && ok;
    ok = fill({x + width - margin - edge, y + edge, x + width - margin, y + height - edge}, RGB(255, 255, 255)) && ok;
    constexpr COLORREF bars[] = {RGB(192,192,192), RGB(192,192,0), RGB(0,192,192), RGB(0,192,0),
        RGB(192,0,192), RGB(192,0,0), RGB(0,0,192), RGB(48,48,48)};
    const int left = x + margin + edge, span = width - 2 * (margin + edge);
    for (int i = 0; i < 8; ++i)
        ok = fill({left + span * i / 8, y + height / 12,
            left + span * (i + 1) / 8, y + height / 3}, bars[i]) && ok;
    const int by_height = height / 15, by_width = span / 26;
    const int font_size = by_height < by_width ? by_height : by_width;
    HFONT font = CreateFontW(-font_size, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!font) { RestoreDC(dc, saved); return false; }
    const auto previous = SelectObject(dc, font);
    ok = previous && previous != HGDI_ERROR && SetBkMode(dc, TRANSPARENT) != 0 &&
        SetTextColor(dc, RGB(255,255,255)) != CLR_INVALID && ok;
    const auto line = [&](const wchar_t *text, int top, int bottom) {
        RECT rect{left, y + top, left + span, y + bottom};
        return DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX) != 0;
    };
    ok = line(L"CHATVIEW OUTPUT TEST", height * 3 / 8, height / 2) && ok;
    const auto label = video_check_label(identifier);
    ok = line(label.data(), height / 2, height * 5 / 8) && ok;
    wchar_t detail[96]{};
    const int length = swprintf_s(detail, 96, L"%d x %d  |  SDR  |  %u s", width, height, seconds);
    ok = length > 0 && line(detail, height * 5 / 8, height * 3 / 4) && ok;
    const unsigned step = seconds % 8U;
    ok = fill({left + span * static_cast<int>(step) / 8, y + height * 5 / 6,
        left + span * static_cast<int>(step + 1U) / 8, y + height * 11 / 12}, RGB(255,255,255)) && ok;
    if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
    ok = RestoreDC(dc, saved) != FALSE && ok;
    DeleteObject(font);
    return ok;
}
}
