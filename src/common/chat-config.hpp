// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace chatview {

inline constexpr wchar_t kConfigChangedMessageName[] = L"ChatViewOBS.ConfigChanged.v1";

struct ChatConfig {
    std::wstring url;
};

[[nodiscard]] bool is_supported_chat_url(const std::wstring &url) noexcept;
[[nodiscard]] bool load_chat_config(ChatConfig &config) noexcept;
[[nodiscard]] bool save_chat_config(const ChatConfig &config) noexcept;
[[nodiscard]] bool clear_chat_config() noexcept;
[[nodiscard]] std::wstring webview_user_data_folder() noexcept;

} // namespace chatview
