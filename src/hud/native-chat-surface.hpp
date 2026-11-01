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
    // The selected local document has been closed or its own navigation /
    // process failed. The owner may spend its existing one local recovery
    // attempt without waiting for the midpoint of the first-display budget.
    [[nodiscard]] bool discarded() const noexcept;
    // Receives the versioned chat-snapshot envelope without server metadata.
    // Adds a document-local monotonically increasing frameId and accepts only
    // that exact frame's nonce-bound WebView2 render ACK. No JSON is interpolated
    // into executable script or document markup.
    [[nodiscard]] bool publish(const std::wstring &envelope) noexcept;
    // UI-only command to this ready own WebView2 document. It sends no chat,
    // URL, credential or server message, and generates no render ACK.
    [[nodiscard]] bool jump_to_latest() noexcept;
    void close() noexcept;
    [[nodiscard]] unsigned long long rendered_frames() const noexcept;
    [[nodiscard]] unsigned int rendered_messages() const noexcept;
    [[nodiscard]] unsigned long long rejected_frames() const noexcept;
private:
    friend struct NativeChatSurfaceTestAccess;
    // Test-only readiness evidence; never expose the private URL or nonce.
    struct ReadinessEvidence {
        bool attached;
        bool navigation_started;
        bool nonce_handshake;
        bool source_matches;
    };
    [[nodiscard]] ReadinessEvidence readiness_evidence() const noexcept;
    struct State;
    std::shared_ptr<State> state_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken navigation_token_{};
    EventRegistrationToken completed_token_{};
    EventRegistrationToken message_token_{};
    EventRegistrationToken process_token_{};
};
}
