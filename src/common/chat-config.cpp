// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/chat-config.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace chatview {
namespace {

constexpr wchar_t kConfigSection[] = L"chat";
constexpr wchar_t kUrlKey[] = L"url";
constexpr std::size_t kMaximumUrlLength = 2048U;

std::filesystem::path local_app_data_path() noexcept
{
    try {
        const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0U);
        if (required == 0U) {
            return {};
        }

        std::wstring value(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), required);
        if (written == 0U || written >= required) {
            return {};
        }
        value.resize(written);
        return std::filesystem::path(value) / L"ChatView";
    } catch (...) {
        return {};
    }
}

std::filesystem::path config_file_path() noexcept
{
    const std::filesystem::path root = local_app_data_path();
    return root.empty() ? std::filesystem::path{} : root / L"config.ini";
}

bool has_path_prefix(std::wstring_view path, std::wstring_view prefix) noexcept
{
    return path.size() > prefix.size() && path.substr(0U, prefix.size()) == prefix;
}

bool has_non_empty_query_value(
    std::wstring_view extra_info, std::wstring_view key) noexcept
{
    if (extra_info.empty() || extra_info.front() != L'?') {
        return false;
    }

    extra_info.remove_prefix(1U);
    const std::size_t fragment = extra_info.find(L'#');
    if (fragment != std::wstring_view::npos) {
        extra_info = extra_info.substr(0U, fragment);
    }

    while (!extra_info.empty()) {
        const std::size_t separator = extra_info.find(L'&');
        const std::wstring_view pair = extra_info.substr(0U, separator);
        const std::size_t equals = pair.find(L'=');
        if (equals != std::wstring_view::npos && pair.substr(0U, equals) == key &&
            equals + 1U < pair.size()) {
            return true;
        }

        if (separator == std::wstring_view::npos) {
            break;
        }
        extra_info.remove_prefix(separator + 1U);
    }
    return false;
}

bool is_youtube_host(const std::wstring &host) noexcept
{
    return _wcsicmp(host.c_str(), L"www.youtube.com") == 0 ||
           _wcsicmp(host.c_str(), L"youtube.com") == 0;
}

} // namespace

bool is_supported_chat_url(const std::wstring &url) noexcept
{
    try {
        if (url.empty() || url.size() > kMaximumUrlLength) {
            return false;
        }

        URL_COMPONENTSW components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        components.dwUserNameLength = static_cast<DWORD>(-1);
        components.dwPasswordLength = static_cast<DWORD>(-1);

        if (!WinHttpCrackUrl(url.c_str(), 0U, 0U, &components)) {
            return false;
        }

        if (components.nScheme != INTERNET_SCHEME_HTTPS ||
            components.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
            components.dwUserNameLength != 0U || components.dwPasswordLength != 0U ||
            components.lpszHostName == nullptr || components.lpszUrlPath == nullptr) {
            return false;
        }

        const std::wstring host(
            components.lpszHostName,
            components.lpszHostName + components.dwHostNameLength);
        const std::wstring_view path(components.lpszUrlPath, components.dwUrlPathLength);
        const std::wstring_view extra_info = components.lpszExtraInfo == nullptr
                                                 ? std::wstring_view{}
                                                 : std::wstring_view(
                                                       components.lpszExtraInfo,
                                                       components.dwExtraInfoLength);

        if (_wcsicmp(host.c_str(), L"weflab.com") == 0) {
            return has_path_prefix(path, L"/page/");
        }

        if (_wcsicmp(host.c_str(), L"chzzk.naver.com") == 0) {
            return has_path_prefix(path, L"/chat/");
        }

        if (is_youtube_host(host)) {
            return path == L"/live_chat" &&
                   has_non_empty_query_value(extra_info, L"v");
        }

        return false;
    } catch (...) {
        return false;
    }
}

bool load_chat_config(ChatConfig &config) noexcept
{
    try {
        const std::filesystem::path path = config_file_path();
        if (path.empty()) {
            return false;
        }

        std::array<wchar_t, kMaximumUrlLength + 2U> buffer{};
        const DWORD length = GetPrivateProfileStringW(
            kConfigSection,
            kUrlKey,
            L"",
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            path.c_str());
        if (length == 0U || length >= static_cast<DWORD>(buffer.size() - 1U)) {
            return false;
        }

        ChatConfig loaded{std::wstring(buffer.data(), length)};
        if (!is_supported_chat_url(loaded.url)) {
            return false;
        }

        config = std::move(loaded);
        return true;
    } catch (...) {
        return false;
    }
}

bool save_chat_config(const ChatConfig &config) noexcept
{
    try {
        if (!is_supported_chat_url(config.url)) {
            return false;
        }

        const std::filesystem::path path = config_file_path();
        if (path.empty()) {
            return false;
        }

        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }

        return WritePrivateProfileStringW(
                   kConfigSection, kUrlKey, config.url.c_str(), path.c_str()) != FALSE;
    } catch (...) {
        return false;
    }
}

bool clear_chat_config() noexcept
{
    try {
        const std::filesystem::path path = config_file_path();
        if (path.empty()) {
            return false;
        }

        if (DeleteFileW(path.c_str())) {
            return true;
        }

        return GetLastError() == ERROR_FILE_NOT_FOUND;
    } catch (...) {
        return false;
    }
}

std::wstring webview_user_data_folder() noexcept
{
    try {
        const std::filesystem::path root = local_app_data_path();
        if (root.empty()) {
            return {};
        }

        const std::filesystem::path folder = root / L"WebView2";
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        return error ? std::wstring{} : folder.wstring();
    } catch (...) {
        return {};
    }
}

} // namespace chatview
