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
    [[nodiscard]] DWORD wait_timeout() const noexcept
    {
        // The worker can finish between tick() and the message-loop wait.
        // Keep ticking only while a real panel still displays "Stopping";
        // otherwise its explicit return button may remain disabled forever.
        const bool pending_status = dialog_ && displayed_status_ == NativeChatStatus::Stopping;
        return active_ || auto_connect_pending_ || signing_out_ || login_cancel_waiting_ || retry_first_display_pending_ || client_.running() ||
            pending_status ? 100U : INFINITE;
    }
    void close() noexcept;
private:
    friend struct NativeChatConnectionTestAccess;
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK host_procedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    NativeChatStatus local_status() const noexcept;
    void connect() noexcept;
    void resume_current() noexcept;
    [[nodiscard]] bool can_retry_first_display(NativeChatStatus status) const noexcept;
    void retry_first_display() noexcept;
    void begin(std::wstring origin, std::wstring credential, bool local, DisplayAuthentication mode) noexcept;
    bool open_surface() noexcept;
    void forget() noexcept;
    bool can_open_management(NativeChatStatus status) const noexcept;
    void open_management() noexcept;
    bool can_reopen_login(NativeChatStatus status) const noexcept;
    void reopen_login() noexcept;
    void clear_display(bool preserve_host = false) noexcept;
    void end(const wchar_t *notice, bool preserve_host = false) noexcept;
    void notice(const wchar_t *text) noexcept;
    void show_connection_state() noexcept;
    void show_display_status() noexcept;
    HudWindow &hud_;
    BrowserLauncher browser_;
    const DisplayRole role_;
    std::optional<DisplayConnectionState> connection_state_;
    std::optional<NativeChatStatus> displayed_status_;
    DisplayClient client_;
    NativeChatSurface surface_;
    HWND host_ = nullptr;
    HWND dialog_ = nullptr;
    UINT open_message_ = 0U;
    UINT query_message_ = 0U;
    bool opening_dialog_ = false;
    bool opening_management_ = false;
    bool opening_login_ = false;
    bool closed_ = false;
    bool hotkey_ = false;
    bool active_ = false;
    bool ready_ = false;
    bool awaiting_login_ = false;
    bool auto_connect_pending_ = true;
    bool remembered_ = false;
    bool reconnecting_ = false;
    bool signing_out_ = false;
    bool login_cancel_waiting_ = false; // Await worker evidence, not a server-side revocation.
    bool retry_first_display_pending_ = false; // Explicit one-time same-approval display return.
    bool retry_first_display_label_ = false;
    bool force_new_login_ = false; // Never switch to another remembered account after stopping consent.
    bool approval_denied_ = false; // Last server denial, not authority or a transport-error guess.
    bool pending_subscribed_ = false;
    bool in_flight_subscribed_ = false;
    // A provider interruption must reach and be acknowledged by the same
    // owned DOM before a rapid new subscription can replace its clear frame.
    bool clearing_interrupted_chat_ = false;
    bool in_flight_interruption_ = false;
    bool after_clear_subscribed_ = false;
    std::wstring after_clear_;
    bool displayed_subscribed_ = false;
    bool first_text_rendered_ = false; // First text row acknowledged by this owned DOM only.
    bool first_document_recovery_attempted_ = false; // At most one bounded local retry per approved display start.
    unsigned long long awaiting_frame_ = 0U;
    ULONGLONG render_deadline_ = 0;
    ULONGLONG loading_deadline_ = 0;
    std::wstring pending_;
    std::wstring login_url_; // Current request only; no verifier or approval token.
    std::wstring previous_login_code_; // Only six public code characters while old cancellation is unconfirmed.
    std::wstring service_origin_; // Validated current service only; not an auth capability.
};
}
