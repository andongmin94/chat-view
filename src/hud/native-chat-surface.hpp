// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Windows.h>
#include <objbase.h>
#include <wrl/client.h>
#include <WebView2.h>
#include <memory>
#include <string>

namespace chatview {
class WebViewHost;
// UI-thread-only display boundary. It neither authenticates nor opens a socket.
// The caller owns platform authorization and must stop delivery on lease loss.
class NativeChatSurface final {
public:
    NativeChatSurface() = default;
    ~NativeChatSurface();
    NativeChatSurface(const NativeChatSurface &) = delete;
    NativeChatSurface &operator=(const NativeChatSurface &) = delete;
    [[nodiscard]] bool open(WebViewHost &host) noexcept;
    [[nodiscard]] bool ready() const noexcept;
    // JSON envelope: {type:"chat-snapshot",version:1,snapshot:{state,received,messages}}.
    // No JSON is interpolated into executable script or document markup.
    [[nodiscard]] bool publish(const std::wstring &envelope) noexcept;
    void close() noexcept;
    [[nodiscard]] unsigned long long rendered_frames() const noexcept;
    [[nodiscard]] unsigned int rendered_messages() const noexcept;
    [[nodiscard]] unsigned long long rejected_frames() const noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken navigation_token_{};
    EventRegistrationToken completed_token_{};
    EventRegistrationToken message_token_{};
    EventRegistrationToken process_token_{};
};
}
