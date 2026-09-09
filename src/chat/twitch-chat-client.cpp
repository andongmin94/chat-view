// SPDX-License-Identifier: GPL-2.0-or-later

#include "chat/twitch-chat-client.hpp"

#include "chat/twitch-irc-parser.hpp"
#include "common/utf8.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace chatview {
namespace {

constexpr wchar_t kTwitchChatHost[] = L"irc-ws.chat.twitch.tv";
constexpr wchar_t kTwitchChatPath[] = L"/";
constexpr int kReceiveTimeoutMs = 1000;
constexpr unsigned int kMaximumReconnectDelaySeconds = 15U;

class InternetHandle final {
public:
    InternetHandle() noexcept = default;
    explicit InternetHandle(HINTERNET handle) noexcept : handle_(handle) {}

    ~InternetHandle()
    {
        reset();
    }

    InternetHandle(const InternetHandle &) = delete;
    InternetHandle &operator=(const InternetHandle &) = delete;

    InternetHandle(InternetHandle &&other) noexcept
        : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    InternetHandle &operator=(InternetHandle &&other) noexcept
    {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    [[nodiscard]] HINTERNET get() const noexcept
    {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return handle_ != nullptr;
    }

    HINTERNET release() noexcept
    {
        return std::exchange(handle_, nullptr);
    }

    void reset(HINTERNET replacement = nullptr) noexcept
    {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    HINTERNET handle_ = nullptr;
};

bool send_text(HINTERNET web_socket, std::string_view text) noexcept
{
    if (web_socket == nullptr || text.size() > static_cast<std::size_t>(MAXDWORD)) {
        return false;
    }

    return WinHttpWebSocketSend(
               web_socket,
               WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
               const_cast<char *>(text.data()),
               static_cast<DWORD>(text.size())) == ERROR_SUCCESS;
}

std::string anonymous_nickname()
{
    const std::uint64_t seed = GetTickCount64() ^ static_cast<std::uint64_t>(GetCurrentProcessId());
    const std::uint64_t suffix = 100000U + seed % 900000U;
    return "justinfan" + std::to_string(suffix);
}

bool sleep_with_stop(std::stop_token stop_token, std::chrono::seconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (!stop_token.stop_requested() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !stop_token.stop_requested();
}

} // namespace

TwitchChatClient::~TwitchChatClient()
{
    stop();
}

bool TwitchChatClient::start(TwitchConfig config,
                             MessageHandler message_handler,
                             StatusHandler status_handler)
{
    stop();

    if (config.channel.empty() || !message_handler) {
        return false;
    }
    if (!config.anonymous() && (config.username.empty() || config.oauth_token.empty())) {
        return false;
    }

    config_ = std::move(config);
    message_handler_ = std::move(message_handler);
    status_handler_ = std::move(status_handler);
    worker_ = std::jthread([this](std::stop_token stop_token) {
        run(stop_token);
    });
    return true;
}

void TwitchChatClient::stop() noexcept
{
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }

    if (!config_.oauth_token.empty()) {
        SecureZeroMemory(config_.oauth_token.data(), config_.oauth_token.size());
    }
    config_ = {};
    message_handler_ = {};
    status_handler_ = {};
}

void TwitchChatClient::run(std::stop_token stop_token)
{
    unsigned int reconnect_delay = 1U;

    while (!stop_token.stop_requested()) {
        publish_status(L"Connecting to Twitch chat…");
        const bool clean_exit = run_session(stop_token);
        if (stop_token.stop_requested() || clean_exit) {
            break;
        }

        publish_status(L"Twitch chat disconnected. Retrying…");
        if (!sleep_with_stop(stop_token, std::chrono::seconds(reconnect_delay))) {
            break;
        }
        reconnect_delay = std::min(reconnect_delay * 2U, kMaximumReconnectDelaySeconds);
    }
}

bool TwitchChatClient::run_session(std::stop_token stop_token)
{
    InternetHandle session(WinHttpOpen(
        L"ChatView OBS/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0U));
    if (!session) {
        return false;
    }

    WinHttpSetTimeouts(session.get(), 10000, 10000, 10000, kReceiveTimeoutMs);

    InternetHandle connection(WinHttpConnect(
        session.get(),
        kTwitchChatHost,
        INTERNET_DEFAULT_HTTPS_PORT,
        0U));
    if (!connection) {
        return false;
    }

    InternetHandle request(WinHttpOpenRequest(
        connection.get(),
        L"GET",
        kTwitchChatPath,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request) {
        return false;
    }

    if (!WinHttpSetOption(
            request.get(),
            WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
            nullptr,
            0U) ||
        !WinHttpSendRequest(
            request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0U,
            WINHTTP_NO_REQUEST_DATA,
            0U,
            0U,
            0U) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        return false;
    }

    InternetHandle web_socket(WinHttpWebSocketCompleteUpgrade(request.get(), 0U));
    if (!web_socket) {
        return false;
    }
    request.reset();

    const std::string nickname = config_.anonymous() ? anonymous_nickname() : config_.username;
    const std::string pass = config_.anonymous()
                                 ? "PASS SCHMOOPIIE\r\n"
                                 : "PASS oauth:" + config_.oauth_token + "\r\n";
    const std::array<std::string, 4U> commands = {
        pass,
        "NICK " + nickname + "\r\n",
        "CAP REQ :twitch.tv/tags twitch.tv/commands\r\n",
        "JOIN #" + config_.channel + "\r\n",
    };
    for (const std::string &command : commands) {
        if (!send_text(web_socket.get(), command)) {
            return false;
        }
    }

    publish_status(L"Twitch chat connected: #" + utf8_to_wide(config_.channel));

    IrcLineBuffer line_buffer;
    std::string frame;
    std::array<char, 4096U> receive_buffer{};

    while (!stop_token.stop_requested()) {
        DWORD bytes_read = 0U;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        const DWORD result = WinHttpWebSocketReceive(
            web_socket.get(),
            receive_buffer.data(),
            static_cast<DWORD>(receive_buffer.size()),
            &bytes_read,
            &buffer_type);

        if (result == ERROR_WINHTTP_TIMEOUT) {
            continue;
        }
        if (result != ERROR_SUCCESS) {
            return false;
        }
        if (buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            return false;
        }

        const bool utf8_fragment =
            buffer_type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE;
        const bool utf8_message =
            buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        if (!utf8_fragment && !utf8_message) {
            continue;
        }

        frame.append(receive_buffer.data(), bytes_read);
        if (!utf8_message) {
            continue;
        }

        const std::vector<std::string> lines = line_buffer.append(frame);
        frame.clear();

        for (const std::string &line : lines) {
            if (const std::optional<std::string> pong = twitch_pong_for_ping(line)) {
                if (!send_text(web_socket.get(), *pong)) {
                    return false;
                }
                continue;
            }

            if (line == "RECONNECT") {
                return false;
            }
            if (line.find("Login authentication failed") != std::string::npos) {
                publish_status(L"Twitch authentication failed. Run configure-twitch.ps1 again.");
                return false;
            }

            const std::optional<ParsedChatMessage> parsed = parse_twitch_privmsg(line);
            if (!parsed.has_value()) {
                continue;
            }

            try {
                message_handler_(
                    utf8_to_wide(parsed->author),
                    utf8_to_wide(parsed->text));
            } catch (...) {
                return false;
            }
        }
    }

    WinHttpWebSocketClose(
        web_socket.get(),
        WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
        nullptr,
        0U);
    return true;
}

void TwitchChatClient::publish_status(std::wstring status) noexcept
{
    if (!status_handler_) {
        return;
    }

    try {
        status_handler_(std::move(status));
    } catch (...) {
    }
}

} // namespace chatview
