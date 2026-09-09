// SPDX-License-Identifier: GPL-2.0-or-later

#include "chat/twitch-config.hpp"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (L"chatview-twitch-config-test-" + std::to_wstring(GetCurrentProcessId()));

    std::error_code filesystem_error;
    std::filesystem::remove_all(root, filesystem_error);
    filesystem_error.clear();
    std::filesystem::create_directories(root / L"ChatView", filesystem_error);
    if (filesystem_error) {
        return fail("Failed to create the Twitch configuration test directory");
    }

    const std::wstring root_string = root.wstring();
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA", root_string.c_str())) {
        return fail("Failed to redirect LOCALAPPDATA");
    }

    std::wstring error;
    if (chatview::load_twitch_config(error).has_value() || !error.empty()) {
        return fail("A missing Twitch configuration was not treated as disabled");
    }

    const std::filesystem::path path = root / L"ChatView" / L"twitch.ini";
    if (!WritePrivateProfileStringW(L"twitch", L"enabled", L"1", path.c_str()) ||
        !WritePrivateProfileStringW(L"twitch", L"channel", L"#Test_Channel", path.c_str()) ||
        !WritePrivateProfileStringW(L"twitch", L"username", L"", path.c_str()) ||
        !WritePrivateProfileStringW(L"twitch", L"token_dpapi", L"", path.c_str())) {
        return fail("Failed to create an anonymous Twitch configuration");
    }

    const auto anonymous = chatview::load_twitch_config(error);
    if (!anonymous.has_value() || !anonymous->anonymous() ||
        anonymous->channel != "test_channel" || !error.empty()) {
        return fail("Anonymous Twitch configuration did not load correctly");
    }

    if (!WritePrivateProfileStringW(L"twitch", L"channel", L"invalid channel", path.c_str())) {
        return fail("Failed to create an invalid Twitch configuration");
    }
    if (chatview::load_twitch_config(error).has_value() || error.empty()) {
        return fail("An invalid Twitch channel was accepted");
    }

    if (!WritePrivateProfileStringW(L"twitch", L"enabled", L"0", path.c_str())) {
        return fail("Failed to disable the Twitch configuration");
    }
    error.clear();
    if (chatview::load_twitch_config(error).has_value() || !error.empty()) {
        return fail("A disabled Twitch configuration was loaded");
    }

    filesystem_error.clear();
    std::filesystem::remove_all(root, filesystem_error);
    if (filesystem_error) {
        return fail("Failed to remove the Twitch configuration test directory");
    }

    return 0;
}
