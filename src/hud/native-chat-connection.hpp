// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud/display-client.hpp"
#include "hud/native-chat-surface.hpp"
#include "common/native-chat-control.hpp"
#include <Windows.h>
#include <optional>
#include <string>

namespace chatview {
class HudWindow;
class NativeChatConnection final {
public:
    using BrowserLauncher = bool (*)(HWND, const wchar_t *);
    explicit NativeChatConnection(HudWindow &hud, DisplayRole role, BrowserLauncher browser = nullptr) noexcept;
    ~NativeChatConnection();
    NativeChatConnection(const NativeChatConnection &) = delete;
    NativeChatConnection &operator=(const NativeChatConnection &) = delete;
    bool dispatch(MSG &message) noexcept;
    bool open_dialog() noexcept;
    void tick() noexcept;
    void observe_outputs(ObsOutputObservation sample) noexcept { client_.observe_outputs(sample); }
    [[nodiscard]] DWORD wait_timeout() const noexcept { return active_ || auto_connect_pending_ || signing_out_ || client_.running() ? 100U : INFINITE; }
    void close() noexcept;
private:
    friend struct NativeChatConnectionTestAccess;
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK host_procedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    NativeChatStatus local_status() const noexcept;
    void connect() noexcept;
    void resume_current() noexcept;
    void begin(std::wstring origin, std::wstring credential, bool local, DisplayAuthentication mode) noexcept;
    bool open_surface() noexcept;
    void forget() noexcept;
    void clear_display(bool preserve_host = false) noexcept;
    void end(const wchar_t *notice, bool preserve_host = false) noexcept;
    void notice(const wchar_t *text) noexcept;
    void show_connection_state() noexcept;
    HudWindow &hud_;
    BrowserLauncher browser_;
    const DisplayRole role_;
    std::optional<DisplayConnectionState> connection_state_;
    DisplayClient client_;
    NativeChatSurface surface_;
    HWND host_ = nullptr;
    HWND dialog_ = nullptr;
    UINT open_message_ = 0U;
    UINT query_message_ = 0U;
    bool opening_dialog_ = false;
    bool closed_ = false;
    bool hotkey_ = false;
    bool active_ = false;
    bool ready_ = false;
    bool awaiting_login_ = false;
    bool auto_connect_pending_ = true;
    bool remembered_ = false;
    bool reconnecting_ = false;
    bool signing_out_ = false;
    bool pending_subscribed_ = false;
    bool in_flight_subscribed_ = false;
    bool displayed_subscribed_ = false;
    unsigned long long awaiting_frame_ = 0U;
    ULONGLONG render_deadline_ = 0;
    ULONGLONG loading_deadline_ = 0;
    std::wstring pending_;
};
}
