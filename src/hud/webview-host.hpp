// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <winhttp.h>
#include <wrl/client.h>

#include <WebView2.h>

#include <memory>
#include <string>

namespace chatview {

inline constexpr UINT kWebViewReadyMessage = WM_APP + 40U;
inline constexpr UINT kWebViewFailedMessage = WM_APP + 41U;
inline constexpr UINT kWebViewDocumentReadyMessage = WM_APP + 42U;

class WebViewHost final {
public:
    WebViewHost();
    ~WebViewHost();

    WebViewHost(const WebViewHost &) = delete;
    WebViewHost &operator=(const WebViewHost &) = delete;

    [[nodiscard]] bool initialize(HWND window) noexcept;
    void close() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    void resize() noexcept;
    void notify_parent_position_changed() noexcept;
    [[nodiscard]] bool navigate(const std::wstring &url) noexcept;
    void show_setup_page() noexcept;
    void set_host_state(
        bool editing,
        const std::wstring &status_text,
        const std::wstring &status_tone) noexcept;
    [[nodiscard]] bool forward_mouse_message(
        UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    void focus() noexcept;

private:
    struct CallbackState {
        WebViewHost *owner = nullptr;
    };

    [[nodiscard]] HRESULT initialize_composition() noexcept;
    HRESULT on_environment_created(
        HRESULT result, ICoreWebView2Environment *environment) noexcept;
    HRESULT on_controller_created(
        HRESULT result, ICoreWebView2CompositionController *controller) noexcept;
    HRESULT on_bootstrap_registered(HRESULT result) noexcept;
    HRESULT on_navigation_completed(
        ICoreWebView2NavigationCompletedEventArgs *args) noexcept;
    HRESULT on_process_failed(ICoreWebView2ProcessFailedEventArgs *args) noexcept;
    [[nodiscard]] HRESULT configure_settings() noexcept;
    [[nodiscard]] HRESULT finish_controller_initialization() noexcept;
    void show_offline_page(COREWEBVIEW2_WEB_ERROR_STATUS status) noexcept;
    void post_failure(HRESULT result) const noexcept;
    void apply_host_state() noexcept;

    HWND window_ = nullptr;
    bool ready_ = false;
    bool editing_ = false;
    std::wstring current_url_;
    std::wstring status_text_;
    std::wstring status_tone_ = L"#aeb0b2";
    std::shared_ptr<CallbackState> callback_state_;

    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> composition_device_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> composition_target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> root_visual_;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2CompositionController> composition_controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken navigation_starting_token_{};
    EventRegistrationToken navigation_completed_token_{};
    EventRegistrationToken new_window_requested_token_{};
    EventRegistrationToken process_failed_token_{};
};

} // namespace chatview
