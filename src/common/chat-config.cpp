// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/chat-config.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace chatview {
namespace {

constexpr wchar_t kConfigSection[] = L"chat";
constexpr wchar_t kUrlKey[] = L"url";
constexpr std::size_t kMaximumUrlLength = 2048U;
constexpr std::size_t kChzzkChannelIdLength = 32U;
constexpr std::size_t kMaximumYouTubeVideoIdLength = 64U;

struct ParsedUrl {
    std::wstring host;
    std::wstring_view path;
    std::wstring_view extra;
};

std::filesystem::path local_app_data_path() noexcept
{
    try {
        const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0U);
        if (required == 0U) {
            return {};
        }

        std::wstring value(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(
            L"LOCALAPPDATA", value.data(), required);
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

std::optional<ParsedUrl> parse_https_url(const std::wstring &url) noexcept
{
    if (url.empty() || url.size() > kMaximumUrlLength) {
        return std::nullopt;
    }

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    components.dwUserNameLength = static_cast<DWORD>(-1);
    components.dwPasswordLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), 0U, 0U, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS ||
        components.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
        components.dwUserNameLength != 0U ||
        components.dwPasswordLength != 0U ||
        components.lpszHostName == nullptr ||
        components.lpszUrlPath == nullptr) {
        return std::nullopt;
    }

    ParsedUrl parsed;
    parsed.host.assign(
        components.lpszHostName,
        components.lpszHostName + components.dwHostNameLength);
    parsed.path = std::wstring_view(
        components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo != nullptr) {
        parsed.extra = std::wstring_view(
            components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    return parsed;
}

std::wstring_view query_without_fragment(std::wstring_view extra) noexcept
{
    const std::size_t fragment = extra.find(L'#');
    return fragment == std::wstring_view::npos
               ? extra
               : extra.substr(0U, fragment);
}

std::optional<std::wstring_view> query_value(
    std::wstring_view extra, std::wstring_view key) noexcept
{
    extra = query_without_fragment(extra);
    if (extra.empty() || extra.front() != L'?') {
        return std::nullopt;
    }

    extra.remove_prefix(1U);
    while (!extra.empty()) {
        const std::size_t separator = extra.find(L'&');
        const std::wstring_view pair = extra.substr(0U, separator);
        const std::size_t equals = pair.find(L'=');
        if (equals != std::wstring_view::npos &&
            pair.substr(0U, equals) == key &&
            equals + 1U < pair.size()) {
            return pair.substr(equals + 1U);
        }

        if (separator == std::wstring_view::npos) {
            break;
        }
        extra.remove_prefix(separator + 1U);
    }
    return std::nullopt;
}

std::optional<std::wstring_view> path_segment_after(
    std::wstring_view path, std::wstring_view prefix) noexcept
{
    if (path.size() <= prefix.size() ||
        path.substr(0U, prefix.size()) != prefix) {
        return std::nullopt;
    }

    std::wstring_view segment = path.substr(prefix.size());
    if (!segment.empty() && segment.back() == L'/') {
        segment.remove_suffix(1U);
    }
    if (segment.empty() || segment.find(L'/') != std::wstring_view::npos) {
        return std::nullopt;
    }
    return segment;
}

bool is_ascii_identifier(
    std::wstring_view value,
    std::size_t minimum,
    std::size_t maximum) noexcept
{
    if (value.size() < minimum || value.size() > maximum) {
        return false;
    }

    for (const wchar_t character : value) {
        const bool letter =
            (character >= L'a' && character <= L'z') ||
            (character >= L'A' && character <= L'Z');
        const bool digit = character >= L'0' && character <= L'9';
        if (!letter && !digit && character != L'_' && character != L'-') {
            return false;
        }
    }
    return true;
}

bool is_chzzk_channel_id(std::wstring_view value) noexcept
{
    if (value.size() != kChzzkChannelIdLength) {
        return false;
    }

    for (const wchar_t character : value) {
        const bool digit = character >= L'0' && character <= L'9';
        const bool lower_hex = character >= L'a' && character <= L'f';
        const bool upper_hex = character >= L'A' && character <= L'F';
        if (!digit && !lower_hex && !upper_hex) {
            return false;
        }
    }
    return true;
}

bool is_host(const std::wstring &host, const wchar_t *expected) noexcept
{
    return _wcsicmp(host.c_str(), expected) == 0;
}

bool is_youtube_host(const std::wstring &host) noexcept
{
    return is_host(host, L"youtube.com") ||
           is_host(host, L"www.youtube.com") ||
           is_host(host, L"m.youtube.com");
}

std::wstring canonical_chzzk_chat_url(std::wstring_view channel_id)
{
    std::wstring result = L"https://chzzk.naver.com/chat/";
    result.append(channel_id);
    return result;
}

std::wstring canonical_youtube_chat_url(std::wstring_view video_id)
{
    std::wstring result =
        L"https://www.youtube.com/live_chat?is_popout=1&v=";
    result.append(video_id);
    return result;
}

std::wstring normalize_weflab_url(
    const ParsedUrl &parsed, const std::wstring &original)
{
    if (!is_host(parsed.host, L"weflab.com") ||
        !path_segment_after(parsed.path, L"/page/").has_value()) {
        return {};
    }

    const std::size_t fragment = original.find(L'#');
    return fragment == std::wstring::npos
               ? original
               : original.substr(0U, fragment);
}

std::wstring normalize_chzzk_url(const ParsedUrl &parsed)
{
    if (!is_host(parsed.host, L"chzzk.naver.com") &&
        !is_host(parsed.host, L"m.chzzk.naver.com")) {
        return {};
    }

    std::optional<std::wstring_view> channel_id =
        path_segment_after(parsed.path, L"/chat/");
    if (!channel_id.has_value()) {
        channel_id = path_segment_after(parsed.path, L"/live/");
    }
    if (!channel_id.has_value()) {
        channel_id = path_segment_after(parsed.path, L"/");
    }
    if (!channel_id.has_value() || !is_chzzk_channel_id(*channel_id)) {
        return {};
    }

    return canonical_chzzk_chat_url(*channel_id);
}

std::wstring normalize_youtube_url(const ParsedUrl &parsed)
{
    std::optional<std::wstring_view> video_id;

    if (is_host(parsed.host, L"youtu.be")) {
        video_id = path_segment_after(parsed.path, L"/");
    } else if (is_youtube_host(parsed.host)) {
        if (parsed.path == L"/watch" || parsed.path == L"/live_chat") {
            video_id = query_value(parsed.extra, L"v");
        } else {
            video_id = path_segment_after(parsed.path, L"/live/");
            if (!video_id.has_value()) {
                video_id = path_segment_after(parsed.path, L"/embed/");
            }
        }
    }

    if (!video_id.has_value() ||
        !is_ascii_identifier(
            *video_id, 1U, kMaximumYouTubeVideoIdLength)) {
        return {};
    }
    return canonical_youtube_chat_url(*video_id);
}

} // namespace

std::wstring normalize_chat_url(const std::wstring &url) noexcept
{
    try {
        const std::optional<ParsedUrl> parsed = parse_https_url(url);
        if (!parsed.has_value()) {
            return {};
        }

        if (std::wstring normalized = normalize_weflab_url(*parsed, url);
            !normalized.empty()) {
            return normalized;
        }
        if (std::wstring normalized = normalize_chzzk_url(*parsed);
            !normalized.empty()) {
            return normalized;
        }
        return normalize_youtube_url(*parsed);
    } catch (...) {
        return {};
    }
}

bool is_supported_chat_url(const std::wstring &url) noexcept
{
    return !normalize_chat_url(url).empty();
}

bool is_supported_chat_document_url(const std::wstring &url) noexcept
{
    try {
        const std::optional<ParsedUrl> parsed = parse_https_url(url);
        if (!parsed.has_value()) {
            return false;
        }

        if (is_host(parsed->host, L"weflab.com")) {
            return path_segment_after(parsed->path, L"/page/").has_value();
        }

        if (is_host(parsed->host, L"chzzk.naver.com")) {
            const std::optional<std::wstring_view> channel_id =
                path_segment_after(parsed->path, L"/chat/");
            return channel_id.has_value() &&
                   is_chzzk_channel_id(*channel_id);
        }

        if (is_youtube_host(parsed->host) &&
            parsed->path == L"/live_chat") {
            const std::optional<std::wstring_view> video_id =
                query_value(parsed->extra, L"v");
            return video_id.has_value() &&
                   is_ascii_identifier(
                       *video_id, 1U, kMaximumYouTubeVideoIdLength);
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
        if (length == 0U ||
            length >= static_cast<DWORD>(buffer.size() - 1U)) {
            return false;
        }

        const std::wstring normalized =
            normalize_chat_url(std::wstring(buffer.data(), length));
        if (normalized.empty()) {
            return false;
        }

        config = ChatConfig{normalized};
        return true;
    } catch (...) {
        return false;
    }
}

bool save_chat_config(const ChatConfig &config) noexcept
{
    try {
        const std::wstring normalized = normalize_chat_url(config.url);
        if (normalized.empty()) {
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
                   kConfigSection,
                   kUrlKey,
                   normalized.c_str(),
                   path.c_str()) != FALSE;
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
