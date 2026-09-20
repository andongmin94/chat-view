// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <string>
#include <thread>

namespace chatview {
enum class DisplayStatus { Idle, Connecting, AwaitingLogin, Receiving, Reconnecting, Denied, SignedOut, Ended, Failed };
enum class DisplayAuthentication { OneTime, Remember, Saved, SignOut, Browser, BrowserRemember };
struct DisplayUpdate {
    DisplayStatus status = DisplayStatus::Idle;
    std::wstring envelope;
    bool subscribed = false;
};
// Owner-thread API, worker-owned asynchronous WinHTTP. Remembered session credentials are
// separate from short display tokens and never enter the renderer or OBS.
class DisplayClient final {
public:
    DisplayClient() = default;
    ~DisplayClient();
    DisplayClient(const DisplayClient &) = delete;
    DisplayClient &operator=(const DisplayClient &) = delete;
    [[nodiscard]] bool start(std::wstring origin, std::wstring credential,
                             bool developer_loopback = false,
                             DisplayAuthentication authentication = DisplayAuthentication::OneTime) noexcept;
    void stop() noexcept;
    // One browser URL, built from the validated origin and a fixed login path.
    [[nodiscard]] bool take_login_url(std::wstring &url) noexcept;
    [[nodiscard]] bool take(DisplayUpdate &update) noexcept;
    [[nodiscard]] bool running() const noexcept;
private:
    struct State;
    static void run(const std::shared_ptr<State> &state, std::wstring origin,
                    std::wstring credential, bool developer_loopback,
                    DisplayAuthentication authentication) noexcept;
    std::shared_ptr<State> state_;
    std::thread worker_;
};
} // namespace chatview
