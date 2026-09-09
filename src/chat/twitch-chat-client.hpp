// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "chat/twitch-config.hpp"

#include <functional>
#include <string>
#include <thread>

namespace chatview {

class TwitchChatClient final {
public:
    using MessageHandler = std::function<void(std::wstring author, std::wstring text)>;
    using StatusHandler = std::function<void(std::wstring status)>;

    TwitchChatClient() = default;
    ~TwitchChatClient();

    TwitchChatClient(const TwitchChatClient &) = delete;
    TwitchChatClient &operator=(const TwitchChatClient &) = delete;

    [[nodiscard]] bool start(
        TwitchConfig config,
        MessageHandler message_handler,
        StatusHandler status_handler = {});
    void stop() noexcept;

private:
    void run(std::stop_token stop_token);
    [[nodiscard]] bool run_session(std::stop_token stop_token);
    void publish_status(std::wstring status) noexcept;

    TwitchConfig config_;
    MessageHandler message_handler_;
    StatusHandler status_handler_;
    std::jthread worker_;
};

} // namespace chatview
