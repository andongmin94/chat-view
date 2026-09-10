// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace chatview {

struct ChatConfig {
    std::wstring url;
};

[[nodiscard]] std::wstring normalize_chat_url(const std::wstring &url) noexcept;
[[nodiscard]] bool is_supported_chat_url(const std::wstring &url) noexcept;
[[nodiscard]] bool is_supported_chat_document_url(
    const std::wstring &url) noexcept;

[[nodiscard]] inline bool is_matching_chat_document_url(
    const std::wstring &candidate,
    const std::wstring &configured_url) noexcept
{
    if (!is_supported_chat_document_url(candidate)) {
        return false;
    }

    const std::wstring normalized_candidate = normalize_chat_url(candidate);
    const std::wstring normalized_configured =
        normalize_chat_url(configured_url);
    return !normalized_candidate.empty() &&
           normalized_candidate == normalized_configured;
}

[[nodiscard]] bool load_chat_config(ChatConfig &config) noexcept;
[[nodiscard]] bool save_chat_config(const ChatConfig &config) noexcept;
[[nodiscard]] bool clear_chat_config() noexcept;
[[nodiscard]] std::wstring webview_user_data_folder() noexcept;

} // namespace chatview
