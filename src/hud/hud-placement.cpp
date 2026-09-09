// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/hud-placement.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace chatview {
namespace {

constexpr wchar_t kPlacementSection[] = L"placement";
constexpr wchar_t kMonitorKey[] = L"monitor";
constexpr wchar_t kOffsetXDipKey[] = L"offset_x_dip";
constexpr wchar_t kOffsetYDipKey[] = L"offset_y_dip";
constexpr wchar_t kWidthDipKey[] = L"width_dip";
constexpr wchar_t kHeightDipKey[] = L"height_dip";
constexpr UINT kDefaultDpi = 96U;

struct MonitorLookupContext {
    const std::wstring *device = nullptr;
    HMONITOR monitor = nullptr;
};

std::filesystem::path placement_file_path()
{
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0U);
    if (required == 0U) {
        return {};
    }

    std::wstring local_app_data(required, L'\0');
    const DWORD written =
        GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(), required);
    if (written == 0U || written >= required) {
        return {};
    }
    local_app_data.resize(written);

    return std::filesystem::path(local_app_data) / L"ChatView" / L"hud.ini";
}

bool read_profile_value(const std::filesystem::path &path,
                        const wchar_t *key,
                        wchar_t *buffer,
                        DWORD buffer_size) noexcept
{
    const DWORD length = GetPrivateProfileStringW(
        kPlacementSection, key, L"", buffer, buffer_size, path.c_str());
    return length > 0U && length < buffer_size - 1U;
}

bool parse_int(const wchar_t *text, int &value) noexcept
{
    errno = 0;
    wchar_t *end = nullptr;
    const long parsed = std::wcstol(text, &end, 10);
    if (errno == ERANGE || end == nullptr || end == text || *end != L'\0' ||
        parsed < static_cast<long>(std::numeric_limits<int>::min()) ||
        parsed > static_cast<long>(std::numeric_limits<int>::max())) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

bool get_monitor_info(HMONITOR monitor, MONITORINFOEXW &info) noexcept
{
    if (monitor == nullptr) {
        return false;
    }

    info = {};
    info.cbSize = sizeof(info);
    return GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info)) != FALSE;
}

BOOL CALLBACK find_monitor_by_device(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    auto *context = reinterpret_cast<MonitorLookupContext *>(data);
    if (context == nullptr || context->device == nullptr) {
        return FALSE;
    }

    MONITORINFOEXW info{};
    if (!get_monitor_info(monitor, info)) {
        return TRUE;
    }

    if (_wcsicmp(info.szDevice, context->device->c_str()) == 0) {
        context->monitor = monitor;
        return FALSE;
    }
    return TRUE;
}

HMONITOR resolve_monitor(const HudPlacement &placement) noexcept
{
    if (placement.valid && !placement.monitor_device.empty()) {
        MonitorLookupContext context{&placement.monitor_device, nullptr};
        EnumDisplayMonitors(
            nullptr, nullptr, &find_monitor_by_device, reinterpret_cast<LPARAM>(&context));
        if (context.monitor != nullptr) {
            return context.monitor;
        }
    }

    const POINT origin{0, 0};
    return MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
}

UINT monitor_dpi(HMONITOR monitor) noexcept
{
    using GetDpiForMonitorFunction = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);

    const HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore == nullptr) {
        return kDefaultDpi;
    }

    const auto get_dpi_for_monitor = reinterpret_cast<GetDpiForMonitorFunction>(
        GetProcAddress(shcore, "GetDpiForMonitor"));
    UINT dpi_x = kDefaultDpi;
    UINT dpi_y = kDefaultDpi;
    if (get_dpi_for_monitor == nullptr ||
        FAILED(get_dpi_for_monitor(monitor, 0, &dpi_x, &dpi_y))) {
        dpi_x = kDefaultDpi;
    }
    FreeLibrary(shcore);
    return dpi_x == 0U ? kDefaultDpi : dpi_x;
}

UINT window_dpi(HWND window) noexcept
{
    using GetDpiForWindowFunction = UINT(WINAPI *)(HWND);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto get_dpi_for_window = reinterpret_cast<GetDpiForWindowFunction>(
        GetProcAddress(user32, "GetDpiForWindow"));
    if (get_dpi_for_window == nullptr) {
        return kDefaultDpi;
    }

    const UINT dpi = get_dpi_for_window(window);
    return dpi == 0U ? kDefaultDpi : dpi;
}

int dip_to_pixel(int value, UINT dpi) noexcept
{
    return MulDiv(value, static_cast<int>(dpi), static_cast<int>(kDefaultDpi));
}

int pixel_to_dip(int value, UINT dpi) noexcept
{
    return MulDiv(value, static_cast<int>(kDefaultDpi), static_cast<int>(dpi));
}

void append_profile_entry(
    std::wstring &section, const wchar_t *key, const std::wstring &value)
{
    section.append(key);
    section.push_back(L'=');
    section.append(value);
    section.push_back(L'\0');
}

bool valid_size(const HudPlacement &placement) noexcept
{
    return placement.width_dip >= kMinimumHudWidthDip &&
           placement.width_dip <= kMaximumHudWidthDip &&
           placement.height_dip >= kMinimumHudHeightDip &&
           placement.height_dip <= kMaximumHudHeightDip;
}

} // namespace

bool load_hud_placement(HudPlacement &placement) noexcept
{
    try {
        const std::filesystem::path path = placement_file_path();
        if (path.empty()) {
            return false;
        }

        std::array<wchar_t, 128U> monitor{};
        std::array<wchar_t, 32U> offset_x{};
        std::array<wchar_t, 32U> offset_y{};
        std::array<wchar_t, 32U> width{};
        std::array<wchar_t, 32U> height{};
        if (!read_profile_value(
                path, kMonitorKey, monitor.data(), static_cast<DWORD>(monitor.size())) ||
            !read_profile_value(
                path, kOffsetXDipKey, offset_x.data(), static_cast<DWORD>(offset_x.size())) ||
            !read_profile_value(
                path, kOffsetYDipKey, offset_y.data(), static_cast<DWORD>(offset_y.size())) ||
            !read_profile_value(
                path, kWidthDipKey, width.data(), static_cast<DWORD>(width.size())) ||
            !read_profile_value(
                path, kHeightDipKey, height.data(), static_cast<DWORD>(height.size()))) {
            return false;
        }

        HudPlacement loaded;
        loaded.monitor_device = monitor.data();
        if (!parse_int(offset_x.data(), loaded.offset_x_dip) ||
            !parse_int(offset_y.data(), loaded.offset_y_dip) ||
            !parse_int(width.data(), loaded.width_dip) ||
            !parse_int(height.data(), loaded.height_dip) || loaded.monitor_device.empty() ||
            !valid_size(loaded)) {
            return false;
        }

        loaded.valid = true;
        placement = std::move(loaded);
        return true;
    } catch (...) {
        return false;
    }
}

bool save_hud_placement(const HudPlacement &placement) noexcept
{
    try {
        if (!placement.valid || placement.monitor_device.empty() || !valid_size(placement)) {
            return false;
        }

        const std::filesystem::path path = placement_file_path();
        if (path.empty()) {
            return false;
        }

        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }

        std::wstring section;
        append_profile_entry(section, kMonitorKey, placement.monitor_device);
        append_profile_entry(
            section, kOffsetXDipKey, std::to_wstring(placement.offset_x_dip));
        append_profile_entry(
            section, kOffsetYDipKey, std::to_wstring(placement.offset_y_dip));
        append_profile_entry(section, kWidthDipKey, std::to_wstring(placement.width_dip));
        append_profile_entry(section, kHeightDipKey, std::to_wstring(placement.height_dip));
        section.push_back(L'\0');

        return WritePrivateProfileSectionW(
                   kPlacementSection, section.c_str(), path.c_str()) != FALSE;
    } catch (...) {
        return false;
    }
}

RECT resolve_hud_bounds(const HudPlacement &placement, int margin_dip) noexcept
{
    const HMONITOR monitor = resolve_monitor(placement);
    MONITORINFOEXW info{};
    if (!get_monitor_info(monitor, info)) {
        return {};
    }

    const UINT dpi = monitor_dpi(monitor);
    const int width_dip = placement.valid ? placement.width_dip : kDefaultHudWidthDip;
    const int height_dip = placement.valid ? placement.height_dip : kDefaultHudHeightDip;
    int width = dip_to_pixel(width_dip, dpi);
    int height = dip_to_pixel(height_dip, dpi);
    const int work_width = static_cast<int>(info.rcWork.right - info.rcWork.left);
    const int work_height = static_cast<int>(info.rcWork.bottom - info.rcWork.top);
    width = std::clamp(width, 1, std::max(1, work_width));
    height = std::clamp(height, 1, std::max(1, work_height));

    LONG x = 0;
    LONG y = 0;
    if (placement.valid) {
        x = info.rcWork.left + dip_to_pixel(placement.offset_x_dip, dpi);
        y = info.rcWork.top + dip_to_pixel(placement.offset_y_dip, dpi);
    } else {
        const int margin = dip_to_pixel(margin_dip, dpi);
        x = info.rcWork.right - width - margin;
        y = info.rcWork.top + margin;
    }

    const LONG maximum_x = std::max(info.rcWork.left, info.rcWork.right - width);
    const LONG maximum_y = std::max(info.rcWork.top, info.rcWork.bottom - height);
    x = std::clamp(x, info.rcWork.left, maximum_x);
    y = std::clamp(y, info.rcWork.top, maximum_y);
    return RECT{x, y, x + width, y + height};
}

HudPlacement capture_hud_placement(HWND window) noexcept
{
    try {
        if (window == nullptr) {
            return {};
        }

        RECT window_rect{};
        if (!GetWindowRect(window, &window_rect)) {
            return {};
        }

        const HMONITOR monitor = MonitorFromRect(&window_rect, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW info{};
        if (!get_monitor_info(monitor, info)) {
            return {};
        }

        const UINT dpi = window_dpi(window);
        HudPlacement placement;
        placement.monitor_device = info.szDevice;
        placement.offset_x_dip = pixel_to_dip(window_rect.left - info.rcWork.left, dpi);
        placement.offset_y_dip = pixel_to_dip(window_rect.top - info.rcWork.top, dpi);
        placement.width_dip = pixel_to_dip(window_rect.right - window_rect.left, dpi);
        placement.height_dip = pixel_to_dip(window_rect.bottom - window_rect.top, dpi);
        placement.width_dip = std::clamp(
            placement.width_dip, kMinimumHudWidthDip, kMaximumHudWidthDip);
        placement.height_dip = std::clamp(
            placement.height_dip, kMinimumHudHeightDip, kMaximumHudHeightDip);
        placement.valid = !placement.monitor_device.empty();
        return placement;
    } catch (...) {
        return {};
    }
}

SIZE minimum_hud_track_size(HWND window) noexcept
{
    const UINT dpi = window_dpi(window);
    return SIZE{
        dip_to_pixel(kMinimumHudWidthDip, dpi),
        dip_to_pixel(kMinimumHudHeightDip, dpi)};
}

} // namespace chatview
