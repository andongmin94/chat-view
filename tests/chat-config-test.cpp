// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/chat-config.hpp"

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
        (L"chatview-config-test-" + std::to_wstring(GetCurrentProcessId()));

    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    if (error) {
        return fail(L"Failed to create the configuration test directory");
    }

    const std::wstring root_string = root.wstring();
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA", root_string.c_str())) {
        return fail(L"Failed to redirect LOCALAPPDATA");
    }

    if (!chatview::is_supported_chat_url(L"https://weflab.com/page/test") ||
        !chatview::is_supported_chat_url(
            L"https://chzzk.naver.com/chat/channel-id?dark=true") ||
        !chatview::is_supported_chat_url(
            L"https://www.youtube.com/live_chat?is_popout=1&v=video-id") ||
        !chatview::is_supported_chat_url(
            L"https://youtube.com/live_chat?v=video-id#chat") ||
        chatview::is_supported_chat_url(L"http://weflab.com/page/test") ||
        chatview::is_supported_chat_url(L"https://weflab.com/not-page/test") ||
        chatview::is_supported_chat_url(L"https://evil.example/page/test") ||
        chatview::is_supported_chat_url(L"https://chzzk.naver.com/chat/") ||
        chatview::is_supported_chat_url(L"https://www.youtube.com/live_chat") ||
        chatview::is_supported_chat_url(L"https://www.youtube.com/watch?v=video-id") ||
        chatview::is_supported_chat_url(
            L"https://user:password@www.youtube.com/live_chat?v=video-id")) {
        return fail(L"Supported chat URL validation returned an unexpected result");
    }

    const chatview::ChatConfig expected{
        L"https://www.youtube.com/live_chat?is_popout=1&v=video-id"};
    if (!chatview::save_chat_config(expected)) {
        return fail(L"Failed to save a valid chat configuration");
    }

    chatview::ChatConfig loaded;
    if (!chatview::load_chat_config(loaded) || loaded.url != expected.url) {
        return fail(L"Chat configuration did not round trip");
    }

    const std::wstring user_data_folder = chatview::webview_user_data_folder();
    if (user_data_folder.empty() ||
        !std::filesystem::is_directory(std::filesystem::path(user_data_folder))) {
        return fail(L"WebView2 user data folder was not created");
    }

    if (!chatview::clear_chat_config()) {
        return fail(L"Failed to clear the chat configuration");
    }
    if (chatview::load_chat_config(loaded)) {
        return fail(L"Cleared chat configuration was still readable");
    }

    error.clear();
    std::filesystem::remove_all(root, error);
    if (error) {
        return fail(L"Failed to remove the configuration test directory");
    }
    return 0;
}
