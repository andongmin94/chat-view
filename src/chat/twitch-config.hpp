// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>

namespace chatview {

struct TwitchConfig {
    std::string channel;
    std::string username;
    std::string oauth_token;

    [[nodiscard]] bool anonymous() const noexcept
    {
        return username.empty() && oauth_token.empty();
    }
};

[[nodiscard]] std::optional<TwitchConfig> load_twitch_config(std::wstring &error) noexcept;

} // namespace chatview
