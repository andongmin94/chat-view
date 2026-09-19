// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <string>
#include <thread>

namespace chatview {
enum class DisplayStatus { Idle, Connecting, Receiving, Ended, Failed };
struct DisplayUpdate {
    DisplayStatus status = DisplayStatus::Idle;
    std::wstring envelope;
    bool subscribed = false;
};

// Owner-thread API, worker-owned asynchronous WinHTTP transport. No HWND,
// WebView, OBS callback, provider credential, or persistent token storage.
class DisplayClient final {
public:
    DisplayClient() = default;
    ~DisplayClient();
    DisplayClient(const DisplayClient &) = delete;
    DisplayClient &operator=(const DisplayClient &) = delete;
    // HTTPS origin only, except explicit literal 127.0.0.1 developer opt-in.
    // The ticket is consumed once; no automatic exchange replay or renewal.
    [[nodiscard]] bool start(std::wstring origin, std::wstring ticket,
                             bool developer_loopback = false) noexcept;
    // Signals cancellation and clears pending content; never waits for network.
    void stop() noexcept;
    // One latest snapshot only, with owner-thread monotonic expiry enforcement.
    [[nodiscard]] bool take(DisplayUpdate &update) noexcept;
    [[nodiscard]] bool running() const noexcept;
private:
    struct State;
    static void run(const std::shared_ptr<State> &state, std::wstring origin,
                    std::wstring ticket, bool developer_loopback) noexcept;
    std::shared_ptr<State> state_;
    std::thread worker_;
};
} // namespace chatview
