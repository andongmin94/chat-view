// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/chat-config.hpp"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace {

constexpr wchar_t kChzzkChannelId[] =
    L"733c98be047f710d3b1bc7a27b0c83e2";
constexpr wchar_t kUpperChzzkChannelId[] =
    L"733C98BE047F710D3B1BC7A27B0C83E2";
constexpr wchar_t kOtherChzzkChannelId[] =
    L"0123456789abcdef0123456789abcdef";
constexpr wchar_t kYouTubeVideoId[] = L"dQw4w9WgXcQ";
constexpr wchar_t kOtherYouTubeVideoId[] = L"abcdefghijk";

int fail(const wchar_t *message)
{
    std::wcerr << message << L'\n';
    return 1;
}

bool expect_normalized(
    const std::wstring &input,
    const std::wstring &expected,
    const wchar_t *message)
{
    if (chatview::normalize_chat_url(input) != expected ||
        !chatview::is_supported_chat_url(input)) {
        std::wcerr << message << L"\ninput: " << input << L"\n";
        return false;
    }
    return true;
}

bool expect_rejected(const std::wstring &input, const wchar_t *message)
{
    if (!chatview::normalize_chat_url(input).empty() ||
        chatview::is_supported_chat_url(input)) {
        std::wcerr << message << L"\ninput: " << input << L"\n";
        return false;
    }
    return true;
}

bool expect_document_allowed(
    const std::wstring &input,
    const wchar_t *message)
{
    if (!chatview::is_supported_chat_document_url(input)) {
        std::wcerr << message << L"\ninput: " << input << L"\n";
        return false;
    }
    return true;
}

bool expect_document_rejected(
    const std::wstring &input,
    const wchar_t *message)
{
    if (chatview::is_supported_chat_document_url(input)) {
        std::wcerr << message << L"\ninput: " << input << L"\n";
        return false;
    }
    return true;
}

bool expect_matching_document(
    const std::wstring &candidate,
    const std::wstring &configured,
    const wchar_t *message)
{
    if (!chatview::is_matching_chat_document_url(candidate, configured)) {
        std::wcerr << message << L"\ncandidate: " << candidate
                   << L"\nconfigured: " << configured << L"\n";
        return false;
    }
    return true;
}

bool expect_nonmatching_document(
    const std::wstring &candidate,
    const std::wstring &configured,
    const wchar_t *message)
{
    if (chatview::is_matching_chat_document_url(candidate, configured)) {
        std::wcerr << message << L"\ncandidate: " << candidate
                   << L"\nconfigured: " << configured << L"\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (L"chatview-config-test-" +
         std::to_wstring(GetCurrentProcessId()));

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

    const std::wstring canonical_weflab =
        L"https://weflab.com/page/test?theme=dark";
    const std::wstring canonical_chzzk =
        std::wstring(L"https://chzzk.naver.com/chat/") +
        kChzzkChannelId;
    const std::wstring other_chzzk =
        std::wstring(L"https://chzzk.naver.com/chat/") +
        kOtherChzzkChannelId;
    const std::wstring canonical_youtube =
        std::wstring(
            L"https://www.youtube.com/live_chat?is_popout=1&v=") +
        kYouTubeVideoId;
    const std::wstring other_youtube =
        std::wstring(
            L"https://www.youtube.com/live_chat?is_popout=1&v=") +
        kOtherYouTubeVideoId;

    if (!expect_normalized(
            L"https://weflab.com/page/test?theme=dark#ignored",
            canonical_weflab,
            L"Weflab URL normalization failed") ||
        !expect_normalized(
            L"HTTPS://WEFLAB.COM/page/test/?theme=dark#ignored",
            canonical_weflab,
            L"Weflab host and trailing slash were not canonicalized") ||
        !expect_normalized(
            L"https://weflab.com/page/test?#ignored",
            L"https://weflab.com/page/test",
            L"Empty Weflab query was not removed") ||
        !expect_normalized(
            std::wstring(L"https://chzzk.naver.com/chat/") +
                kChzzkChannelId + L"?dark=true",
            canonical_chzzk,
            L"CHZZK chat URL normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://CHZZK.NAVER.COM/live/") +
                kUpperChzzkChannelId + L"/#ignored",
            canonical_chzzk,
            L"CHZZK host and channel ID were not canonicalized") ||
        !expect_normalized(
            std::wstring(L"https://m.chzzk.naver.com/live/") +
                kChzzkChannelId,
            canonical_chzzk,
            L"Mobile CHZZK live URL normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://chzzk.naver.com/") +
                kChzzkChannelId,
            canonical_chzzk,
            L"CHZZK channel URL normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://www.youtube.com/watch?v=") +
                kYouTubeVideoId + L"&feature=share",
            canonical_youtube,
            L"YouTube watch URL normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://www.youtube.com/live/") +
                kYouTubeVideoId + L"?si=test",
            canonical_youtube,
            L"YouTube live URL normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://youtu.be/") +
                kYouTubeVideoId + L"?t=5",
            canonical_youtube,
            L"YouTube short URL normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://www.youtube.com/embed/") +
                kYouTubeVideoId,
            canonical_youtube,
            L"YouTube embed URL normalization failed") ||
        !expect_normalized(
            canonical_youtube,
            canonical_youtube,
            L"YouTube live-chat URL normalization failed")) {
        return 1;
    }

    if (!expect_rejected(
            L"http://weflab.com/page/test",
            L"Insecure Weflab URL was accepted") ||
        !expect_rejected(
            L"https://weflab.com/not-page/test",
            L"Unsupported Weflab path was accepted") ||
        !expect_rejected(
            L"https://evil.example/page/test",
            L"Unrelated host was accepted") ||
        !expect_rejected(
            L"https://chzzk.naver.com/live/not-a-channel-id",
            L"Malformed CHZZK channel ID was accepted") ||
        !expect_rejected(
            L"https://www.youtube.com/live_chat",
            L"YouTube live-chat URL without a video ID was accepted") ||
        !expect_rejected(
            L"https://user:password@www.youtube.com/watch?v=dQw4w9WgXcQ",
            L"URL credentials were accepted") ||
        !expect_rejected(
            L"https://www.youtube.com:444/watch?v=dQw4w9WgXcQ",
            L"Non-default HTTPS port was accepted") ||
        !expect_rejected(
            L"https://youtu.be/not/one/segment",
            L"Malformed YouTube short URL was accepted") ||
        !expect_rejected(
            L" https://weflab.com/page/test",
            L"Leading whitespace was accepted") ||
        !expect_rejected(
            L"https://weflab.com/page/test ",
            L"Trailing whitespace was accepted") ||
        !expect_rejected(
            L"https://weflab.com/page/\ttest",
            L"Embedded tab was accepted") ||
        !expect_rejected(
            L"https://weflab.com/page/test\nnext",
            L"Embedded newline was accepted") ||
        !expect_rejected(
            L"https:\\\\weflab.com\\page\\test",
            L"Backslash URL was accepted") ||
        !expect_rejected(
            std::wstring(L"https://weflab.com/page/test") +
                static_cast<wchar_t>(0x7F),
            L"DEL control character was accepted") ||
        !expect_rejected(
            L"https://weflab.com/page/test//",
            L"Multiple trailing path separators were accepted")) {
        return 1;
    }

    if (!expect_document_allowed(
            canonical_weflab,
            L"Canonical Weflab page was not accepted as a document") ||
        !expect_document_allowed(
            canonical_chzzk + L"?dark=true",
            L"Canonical CHZZK chat was not accepted as a document") ||
        !expect_document_allowed(
            canonical_youtube + L"&embed_domain=localhost",
            L"Canonical YouTube live chat was not accepted as a document") ||
        !expect_document_rejected(
            std::wstring(L"https://chzzk.naver.com/live/") +
                kChzzkChannelId,
            L"CHZZK broadcast page was accepted as a chat document") ||
        !expect_document_rejected(
            std::wstring(L"https://chzzk.naver.com/") +
                kChzzkChannelId,
            L"CHZZK channel page was accepted as a chat document") ||
        !expect_document_rejected(
            std::wstring(L"https://www.youtube.com/watch?v=") +
                kYouTubeVideoId,
            L"YouTube watch page was accepted as a chat document") ||
        !expect_document_rejected(
            std::wstring(L"https://www.youtube.com/live/") +
                kYouTubeVideoId,
            L"YouTube live page was accepted as a chat document") ||
        !expect_document_rejected(
            std::wstring(L"https://youtu.be/") +
                kYouTubeVideoId,
            L"YouTube short URL was accepted as a chat document") ||
        !expect_document_rejected(
            L"https://accounts.google.com/ServiceLogin",
            L"External login page was accepted as a chat document")) {
        return 1;
    }

    if (!expect_matching_document(
            canonical_chzzk + L"?dark=true",
            canonical_chzzk,
            L"Matching CHZZK document was rejected") ||
        !expect_matching_document(
            std::wstring(L"https://CHZZK.NAVER.COM/chat/") +
                kUpperChzzkChannelId,
            canonical_chzzk,
            L"Equivalent CHZZK document was rejected") ||
        !expect_matching_document(
            canonical_youtube + L"&embed_domain=localhost",
            canonical_youtube,
            L"Matching YouTube document was rejected") ||
        !expect_matching_document(
            L"HTTPS://WEFLAB.COM/page/test/?theme=dark#ignored",
            canonical_weflab,
            L"Equivalent Weflab document was rejected") ||
        !expect_nonmatching_document(
            other_chzzk,
            canonical_chzzk,
            L"Different CHZZK channel was accepted") ||
        !expect_nonmatching_document(
            other_youtube,
            canonical_youtube,
            L"Different YouTube video was accepted") ||
        !expect_nonmatching_document(
            std::wstring(L"https://chzzk.naver.com/live/") +
                kChzzkChannelId,
            canonical_chzzk,
            L"CHZZK broadcast page matched a chat document") ||
        !expect_nonmatching_document(
            std::wstring(L"https://www.youtube.com/watch?v=") +
                kYouTubeVideoId,
            canonical_youtube,
            L"YouTube watch page matched a chat document") ||
        !expect_nonmatching_document(
            L"https://weflab.com/page/other?theme=dark",
            canonical_weflab,
            L"Different Weflab page was accepted") ||
        !expect_nonmatching_document(
            L"https://weflab.com/page/test?theme=light",
            canonical_weflab,
            L"Different Weflab configuration was accepted")) {
        return 1;
    }

    const chatview::ChatConfig input{
        std::wstring(L"https://www.youtube.com/watch?v=") +
        kYouTubeVideoId};
    if (!chatview::save_chat_config(input)) {
        return fail(L"Failed to save a normal broadcast URL");
    }

    chatview::ChatConfig loaded;
    if (!chatview::load_chat_config(loaded) ||
        loaded.url != canonical_youtube) {
        return fail(
            L"Saved broadcast URL was not persisted in canonical chat form");
    }

    const std::wstring user_data_folder =
        chatview::webview_user_data_folder();
    if (user_data_folder.empty() ||
        !std::filesystem::is_directory(
            std::filesystem::path(user_data_folder))) {
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
