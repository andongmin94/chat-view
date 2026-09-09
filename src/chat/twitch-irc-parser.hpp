// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chatview {

struct ParsedChatMessage {
    std::string author;
    std::string text;
};

class IrcLineBuffer final {
public:
    [[nodiscard]] std::vector<std::string> append(std::string_view bytes);
    void clear() noexcept;

private:
    std::string pending_;
};

[[nodiscard]] std::optional<ParsedChatMessage> parse_twitch_privmsg(std::string_view line);
[[nodiscard]] std::optional<std::string> twitch_pong_for_ping(std::string_view line);

} // namespace chatview
