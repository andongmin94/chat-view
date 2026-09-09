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
constexpr wchar_t kOffsetXKey[] = L"offset_x";
constexpr wchar_t kOffsetYKey[] = L"offset_y";

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

bool parse_long(const wchar_t *text, LONG &value) noexcept
{
    errno = 0;
    wchar_t *end = nullptr;
    const long parsed = std::wcstol(text, &end, 10);
    if (errno == ERANGE || end == text || end == nullptr || *end != L'\0') {
        return false;
    }

    if (parsed < static_cast<long>(std::numeric_limits<LONG>::min()) ||
        parsed > static_cast<long>(std::numeric_limits<LONG>::max())) {
        return false;
    }

    value = static_cast<LONG>(parsed);
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

void append_profile_entry(
    std::wstring &section, const wchar_t *key, const std::wstring &value)
{
    section.append(key);
    section.push_back(L'=');
    section.append(value);
    section.push_back(L'\0');
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
        if (!read_profile_value(
                path, kMonitorKey, monitor.data(), static_cast<DWORD>(monitor.size())) ||
            !read_profile_value(
                path, kOffsetXKey, offset_x.data(), static_cast<DWORD>(offset_x.size())) ||
            !read_profile_value(
                path, kOffsetYKey, offset_y.data(), static_cast<DWORD>(offset_y.size()))) {
            return false;
        }

        HudPlacement loaded;
        if (!parse_long(offset_x.data(), loaded.offset_x) ||
            !parse_long(offset_y.data(), loaded.offset_y)) {
            return false;
        }

        loaded.monitor_device = monitor.data();
        loaded.valid = !loaded.monitor_device.empty();
        if (!loaded.valid) {
            return false;
        }

        placement = std::move(loaded);
        return true;
    } catch (...) {
        return false;
    }
}

bool save_hud_placement(const HudPlacement &placement) noexcept
{
    try {
        if (!placement.valid || placement.monitor_device.empty()) {
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
        append_profile_entry(section, kOffsetXKey, std::to_wstring(placement.offset_x));
        append_profile_entry(section, kOffsetYKey, std::to_wstring(placement.offset_y));
        section.push_back(L'\0');

        return WritePrivateProfileSectionW(kPlacementSection, section.c_str(), path.c_str()) != FALSE;
    } catch (...) {
        return false;
    }
}

POINT resolve_hud_position(
    const HudPlacement &placement, int width, int height, int margin) noexcept
{
    MONITORINFOEXW info{};
    const HMONITOR monitor = resolve_monitor(placement);
    if (!get_monitor_info(monitor, info)) {
        return {};
    }

    POINT position{};
    if (placement.valid) {
        position.x = info.rcWork.left + placement.offset_x;
        position.y = info.rcWork.top + placement.offset_y;
    } else {
        position.x =
            info.rcWork.right - static_cast<LONG>(width) - static_cast<LONG>(margin);
        position.y = info.rcWork.top + static_cast<LONG>(margin);
    }

    const LONG max_x =
        std::max(info.rcWork.left, info.rcWork.right - static_cast<LONG>(width));
    const LONG max_y =
        std::max(info.rcWork.top, info.rcWork.bottom - static_cast<LONG>(height));
    position.x = std::clamp(position.x, info.rcWork.left, max_x);
    position.y = std::clamp(position.y, info.rcWork.top, max_y);
    return position;
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

        HudPlacement placement;
        placement.monitor_device = info.szDevice;
        placement.offset_x = window_rect.left - info.rcWork.left;
        placement.offset_y = window_rect.top - info.rcWork.top;
        placement.valid = !placement.monitor_device.empty();
        return placement;
    } catch (...) {
        return {};
    }
}

} // namespace chatview
