// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/hud-placement.hpp"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace {

int fail(const wchar_t *message)
{
    std::wcerr << message << L'\n';
    return 1;
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (L"chatview-placement-test-" + std::to_wstring(GetCurrentProcessId()));

    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    if (error) {
        return fail(L"Failed to create the placement test directory");
    }

    const std::wstring root_string = root.wstring();
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA", root_string.c_str())) {
        return fail(L"Failed to redirect LOCALAPPDATA");
    }

    chatview::HudPlacement expected;
    expected.monitor_device = L"\\\\.\\DISPLAY_TEST";
    expected.offset_x = -123;
    expected.offset_y = 456;
    expected.valid = true;

    if (!chatview::save_hud_placement(expected)) {
        return fail(L"Failed to save a valid placement");
    }

    chatview::HudPlacement loaded;
    if (!chatview::load_hud_placement(loaded)) {
        return fail(L"Failed to load a saved placement");
    }

    if (!loaded.valid || loaded.monitor_device != expected.monitor_device ||
        loaded.offset_x != expected.offset_x || loaded.offset_y != expected.offset_y) {
        return fail(L"The placement round trip changed data");
    }

    const std::filesystem::path file = root / L"ChatView" / L"hud.ini";
    if (!WritePrivateProfileStringW(
            L"placement", L"offset_x", L"not-a-number", file.c_str())) {
        return fail(L"Failed to corrupt the placement test fixture");
    }

    chatview::HudPlacement unchanged = expected;
    if (chatview::load_hud_placement(unchanged)) {
        return fail(L"Malformed placement data was accepted");
    }

    if (!unchanged.valid || unchanged.monitor_device != expected.monitor_device ||
        unchanged.offset_x != expected.offset_x || unchanged.offset_y != expected.offset_y) {
        return fail(L"A failed load modified the existing placement");
    }

    error.clear();
    std::filesystem::remove_all(root, error);
    if (error) {
        return fail(L"Failed to remove the placement test directory");
    }

    return 0;
}
