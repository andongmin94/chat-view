// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud/connection-status.hpp"
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace chatview {
enum class DisplayStatus { Idle, Connecting, AwaitingLogin, Receiving, Reconnecting, Denied, RoleMismatch, SignedOut, Ended, Failed };
enum class DisplayAuthentication { OneTime, Remember, Saved, SignOut, Browser, BrowserRemember };
struct DisplayUpdate {
    DisplayStatus status = DisplayStatus::Idle;
    std::wstring envelope;
    bool subscribed = false;
    std::optional<DisplayConnectionState> connection;
};
// Owner-thread API, worker-owned asynchronous WinHTTP. App-session credentials
// and connection metadata never enter the private chat renderer or OBS.
class DisplayClient final {
public:
    DisplayClient() = default;
    ~DisplayClient();
    DisplayClient(const DisplayClient &) = delete;
    DisplayClient &operator=(const DisplayClient &) = delete;
    [[nodiscard]] bool start(std::wstring origin, std::wstring credential,
                             bool developer_loopback = false,
                             DisplayAuthentication authentication = DisplayAuthentication::OneTime,
                             DisplayRole role = DisplayRole::Gaming) noexcept;
    void stop() noexcept;
    // Only the validated local OBS transport supplies these observations.
    // Sampling does no I/O; an independent cancellable worker reports them.
    void observe_outputs(ObsOutputObservation sample) noexcept;
    // Cancel delivery immediately; revoke the exact current approval on the
    // worker, whether persisted or not. True means requested, not confirmed.
    // The finished mailbox must report SignedOut before the UI claims success.
    [[nodiscard]] bool sign_out() noexcept;
    [[nodiscard]] bool take_login_url(std::wstring &url) noexcept;
    [[nodiscard]] bool take(DisplayUpdate &update) noexcept;
    [[nodiscard]] bool running() const noexcept;
private:
    struct State;
    static void run(const std::shared_ptr<State> &state, std::wstring origin,
                    std::wstring credential, bool developer_loopback,
                    DisplayAuthentication authentication) noexcept;
    static void run_outputs(const std::shared_ptr<State> &state) noexcept;
    static void run_signout(const std::shared_ptr<State> &state) noexcept;
    std::shared_ptr<State> state_;
    std::thread worker_;
};
} // namespace chatview
