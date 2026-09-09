// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/webview-host.hpp"

#include "common/chat-config.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi.h>
#include <windowsx.h>
#include <wrl.h>
#include <wrl/event.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace chatview {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr wchar_t kSetupPage[] = LR"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ChatView setup</title>
<style>
:root { color-scheme: dark; font-family: "Segoe UI", sans-serif; }
html, body { width:100%; height:100%; margin:0; background:transparent; overflow:hidden; }
body { display:grid; place-items:center; }
main { box-sizing:border-box; width:min(92%, 480px); padding:24px; border:1px solid rgba(255,255,255,.16); border-radius:18px; background:rgba(18,18,22,.92); color:#fff; box-shadow:0 14px 50px rgba(0,0,0,.35); }
h1 { margin:0 0 10px; font-size:22px; }
p { margin:6px 0; color:#c8c8ce; line-height:1.5; }
strong { color:#fff; }
code { color:#7dd3fc; }
</style>
</head>
<body>
<main>
<h1>ChatView needs a chat URL</h1>
<p>Open <strong>OBS Studio → Tools → ChatView Settings…</strong>.</p>
<p>Paste a Weflab page, CHZZK broadcast, or YouTube live URL, then save.</p>
<p><code>Ctrl + Alt + Shift + H</code> unlocks this overlay for moving and resizing.</p>
</main>
</body>
</html>
)HTML";

constexpr wchar_t kOverlayBootstrapScript[] = LR"JS(
(() => {
  try {
    const styleId = '__chatview_transparency_style';
    let pageStyle = document.getElementById(styleId);
    if (!pageStyle) {
      pageStyle = document.createElement('style');
      pageStyle.id = styleId;
      pageStyle.textContent = `
        html, body, body > #root, body > #__next {
          background: transparent !important;
          background-color: transparent !important;
        }
        html {
          --yt-live-chat-background-color: transparent !important;
          --yt-live-chat-secondary-background-color: rgba(18, 18, 22, .70) !important;
          --yt-live-chat-tertiary-background-color: rgba(18, 18, 22, .82) !important;
        }
        yt-live-chat-app,
        yt-live-chat-renderer,
        yt-live-chat-renderer #contents,
        yt-live-chat-renderer #item-list,
        yt-live-chat-renderer #chat {
          background: transparent !important;
          background-color: transparent !important;
        }
        ::-webkit-scrollbar { display: none !important; }
      `;
      (document.head || document.documentElement).appendChild(pageStyle);
    }

    let host = document.getElementById('__chatview_native_host');
    if (!host) {
      host = document.createElement('div');
      host.id = '__chatview_native_host';
      host.style.cssText = 'all:initial;position:fixed;inset:0;z-index:2147483647;pointer-events:none;';
      document.documentElement.appendChild(host);

      const root = host.attachShadow({ mode: 'open' });
      const shadowStyle = document.createElement('style');
      shadowStyle.textContent = `
        :host { all: initial; }
        .frame {
          position: fixed;
          inset: 0;
          box-sizing: border-box;
          border: 3px solid #5ac8fa;
          opacity: 0;
          transition: opacity .12s ease;
        }
        .edit {
          position: absolute;
          left: 12px;
          top: 10px;
          padding: 8px 11px;
          border-radius: 10px;
          background: rgba(20,20,24,.92);
          color: #fff;
          font: 600 13px/1.2 "Segoe UI",sans-serif;
          box-shadow: 0 6px 24px rgba(0,0,0,.35);
        }
        .status {
          position: absolute;
          right: 12px;
          top: 10px;
          display: none;
          align-items: center;
          gap: 7px;
          padding: 7px 10px;
          border-radius: 999px;
          background: rgba(20,20,24,.82);
          color: #fff;
          font: 700 12px/1 "Segoe UI",sans-serif;
          box-shadow: 0 5px 20px rgba(0,0,0,.30);
        }
        .dot {
          width: 8px;
          height: 8px;
          border-radius: 999px;
          background: var(--tone,#aeb0b2);
          box-shadow: 0 0 10px var(--tone,#aeb0b2);
        }
        :host([data-editing="1"]) .frame { opacity: 1; }
        :host([data-has-status="1"]) .status { display: flex; }
      `;

      const frame = document.createElement('div');
      frame.className = 'frame';
      const edit = document.createElement('div');
      edit.className = 'edit';
      edit.textContent = 'DRAG HEADER · RESIZE EDGES · CTRL+ALT+SHIFT+H TO LOCK';
      frame.appendChild(edit);

      const status = document.createElement('div');
      status.className = 'status';
      const dot = document.createElement('span');
      dot.className = 'dot';
      const statusText = document.createElement('span');
      statusText.className = 'statusText';
      status.append(dot, statusText);

      root.append(shadowStyle, frame, status);
      window.__chatviewNative = { host, statusText };
    } else if (!host.isConnected) {
      document.documentElement.appendChild(host);
    }

    window.__chatviewApplyHostState = (state) => {
      const view = window.__chatviewNative;
      if (!view) return;
      view.host.dataset.editing = state.editing ? '1' : '0';
      view.host.dataset.hasStatus = state.status ? '1' : '0';
      view.host.style.setProperty('--tone', state.tone || '#aeb0b2');
      view.statusText.textContent = state.status || '';
    };
    return true;
  } catch (_) {
    return false;
  }
})();
)JS";

std::wstring javascript_string(const std::wstring &value)
{
    std::wstring output;
    output.reserve(value.size() + 2U);
    output.push_back(L'"');
    for (const wchar_t character : value) {
        switch (character) {
        case L'\\':
            output.append(L"\\\\");
            break;
        case L'"':
            output.append(L"\\\"");
            break;
        case L'\r':
            output.append(L"\\r");
            break;
        case L'\n':
            output.append(L"\\n");
            break;
        case L'\t':
            output.append(L"\\t");
            break;
        default:
            output.push_back(character < 0x20 ? L' ' : character);
            break;
        }
    }
    output.push_back(L'"');
    return output;
}

bool is_allowed_host(const std::wstring &host) noexcept
{
    return _wcsicmp(host.c_str(), L"weflab.com") == 0 ||
           _wcsicmp(host.c_str(), L"www.weflab.com") == 0 ||
           _wcsicmp(host.c_str(), L"chzzk.naver.com") == 0 ||
           _wcsicmp(host.c_str(), L"m.chzzk.naver.com") == 0 ||
           _wcsicmp(host.c_str(), L"www.youtube.com") == 0 ||
           _wcsicmp(host.c_str(), L"youtube.com") == 0 ||
           _wcsicmp(host.c_str(), L"m.youtube.com") == 0;
}

bool is_allowed_document_url(const wchar_t *url) noexcept
{
    if (url == nullptr) {
        return false;
    }

    const std::wstring_view value(url);
    if (value == L"about:blank") {
        return true;
    }

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUserNameLength = static_cast<DWORD>(-1);
    components.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url, 0U, 0U, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS ||
        components.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
        components.dwUserNameLength != 0U || components.dwPasswordLength != 0U ||
        components.lpszHostName == nullptr) {
        return false;
    }

    const std::wstring host(
        components.lpszHostName,
        components.lpszHostName + components.dwHostNameLength);
    return is_allowed_host(host);
}

HRESULT create_d3d_device(ComPtr<ID3D11Device> &device) noexcept
{
    constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        nullptr,
        0U,
        D3D11_SDK_VERSION,
        &device,
        nullptr,
        nullptr);
    if (FAILED(result)) {
        result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            nullptr,
            0U,
            D3D11_SDK_VERSION,
            &device,
            nullptr,
            nullptr);
    }
    return result;
}

} // namespace

WebViewHost::WebViewHost() : callback_state_(std::make_shared<CallbackState>())
{
    callback_state_->owner = this;
}

WebViewHost::~WebViewHost()
{
    close();
}

bool WebViewHost::initialize(HWND window) noexcept
{
    if (window == nullptr || window_ != nullptr) {
        return false;
    }

    window_ = window;
    const HRESULT composition_result = initialize_composition();
    if (FAILED(composition_result)) {
        post_failure(composition_result);
        return false;
    }

    const std::wstring user_data_folder = webview_user_data_folder();
    if (user_data_folder.empty()) {
        post_failure(E_FAIL);
        return false;
    }

    SetEnvironmentVariableW(L"WEBVIEW2_DEFAULT_BACKGROUND_COLOR", L"00000000");

    const std::shared_ptr<CallbackState> state = callback_state_;
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        user_data_folder.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [state](HRESULT callback_result, ICoreWebView2Environment *environment) -> HRESULT {
                return state->owner != nullptr
                           ? state->owner->on_environment_created(callback_result, environment)
                           : S_OK;
            })
            .Get());
    if (FAILED(result)) {
        post_failure(result);
        return false;
    }

    return true;
}

void WebViewHost::close() noexcept
{
    if (callback_state_) {
        callback_state_->owner = nullptr;
    }

    ready_ = false;
    if (webview_) {
        if (navigation_starting_token_.value != 0) {
            webview_->remove_NavigationStarting(navigation_starting_token_);
        }
        if (navigation_completed_token_.value != 0) {
            webview_->remove_NavigationCompleted(navigation_completed_token_);
        }
        if (new_window_requested_token_.value != 0) {
            webview_->remove_NewWindowRequested(new_window_requested_token_);
        }
    }

    if (controller_) {
        controller_->Close();
    }
    webview_.Reset();
    controller_.Reset();
    composition_controller_.Reset();
    environment_.Reset();

    if (composition_target_) {
        composition_target_->SetRoot(nullptr);
    }
    if (composition_device_) {
        composition_device_->Commit();
    }
    root_visual_.Reset();
    composition_target_.Reset();
    composition_device_.Reset();
    d3d_device_.Reset();
    window_ = nullptr;
}

bool WebViewHost::ready() const noexcept
{
    return ready_;
}

void WebViewHost::resize() noexcept
{
    if (!controller_ || window_ == nullptr) {
        return;
    }

    RECT bounds{};
    if (GetClientRect(window_, &bounds)) {
        controller_->put_Bounds(bounds);
    }
}

void WebViewHost::notify_parent_position_changed() noexcept
{
    if (controller_) {
        controller_->NotifyParentWindowPositionChanged();
    }
}

bool WebViewHost::navigate(const std::wstring &url) noexcept
{
    if (!ready_ || !webview_ || !is_supported_chat_url(url)) {
        return false;
    }
    return SUCCEEDED(webview_->Navigate(url.c_str()));
}

void WebViewHost::show_setup_page() noexcept
{
    if (ready_ && webview_) {
        webview_->NavigateToString(kSetupPage);
    }
}

void WebViewHost::set_host_state(
    bool editing, const std::wstring &status_text, const std::wstring &status_tone) noexcept
{
    editing_ = editing;
    status_text_ = status_text;
    status_tone_ = status_tone;
    apply_host_state();
}

bool WebViewHost::forward_mouse_message(UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    if (!ready_ || !composition_controller_ || window_ == nullptr) {
        return false;
    }

    switch (message) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        break;
    default:
        return false;
    }

    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    UINT32 mouse_data = 0U;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        ScreenToClient(window_, &point);
        mouse_data = static_cast<UINT32>(
            static_cast<std::int32_t>(GET_WHEEL_DELTA_WPARAM(wparam)));
    } else if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ||
               message == WM_XBUTTONDBLCLK) {
        mouse_data = static_cast<UINT32>(GET_XBUTTON_WPARAM(wparam));
    }

    const auto event_kind = static_cast<COREWEBVIEW2_MOUSE_EVENT_KIND>(message);
    const auto virtual_keys = static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(
        GET_KEYSTATE_WPARAM(wparam));
    return SUCCEEDED(
        composition_controller_->SendMouseInput(event_kind, virtual_keys, mouse_data, point));
}

void WebViewHost::focus() noexcept
{
    if (window_ != nullptr) {
        SetFocus(window_);
    }
    if (controller_) {
        controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}

HRESULT WebViewHost::initialize_composition() noexcept
{
    HRESULT result = create_d3d_device(d3d_device_);
    if (FAILED(result)) {
        return result;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    result = d3d_device_.As(&dxgi_device);
    if (FAILED(result)) {
        return result;
    }

    result = DCompositionCreateDevice(
        dxgi_device.Get(), IID_PPV_ARGS(&composition_device_));
    if (FAILED(result)) {
        return result;
    }

    result = composition_device_->CreateTargetForHwnd(
        window_, TRUE, &composition_target_);
    if (FAILED(result)) {
        return result;
    }

    result = composition_device_->CreateVisual(&root_visual_);
    if (FAILED(result)) {
        return result;
    }

    result = composition_target_->SetRoot(root_visual_.Get());
    if (FAILED(result)) {
        return result;
    }
    return composition_device_->Commit();
}

HRESULT WebViewHost::on_environment_created(
    HRESULT result, ICoreWebView2Environment *environment) noexcept
{
    if (FAILED(result) || environment == nullptr) {
        post_failure(FAILED(result) ? result : E_POINTER);
        return S_OK;
    }

    environment_ = environment;
    ComPtr<ICoreWebView2Environment3> environment3;
    result = environment_.As(&environment3);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    const std::shared_ptr<CallbackState> state = callback_state_;
    result = environment3->CreateCoreWebView2CompositionController(
        window_,
        Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
            [state](HRESULT callback_result,
                    ICoreWebView2CompositionController *controller) -> HRESULT {
                return state->owner != nullptr
                           ? state->owner->on_controller_created(callback_result, controller)
                           : S_OK;
            })
            .Get());
    if (FAILED(result)) {
        post_failure(result);
    }
    return S_OK;
}

HRESULT WebViewHost::on_controller_created(
    HRESULT result, ICoreWebView2CompositionController *controller) noexcept
{
    if (FAILED(result) || controller == nullptr) {
        post_failure(FAILED(result) ? result : E_POINTER);
        return S_OK;
    }

    composition_controller_ = controller;
    result = composition_controller_.As(&controller_);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    result = controller_->get_CoreWebView2(&webview_);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    result = composition_controller_->put_RootVisualTarget(root_visual_.Get());
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(controller_.As(&controller2))) {
        COREWEBVIEW2_COLOR transparent{};
        controller2->put_DefaultBackgroundColor(transparent);
    }

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(webview_->get_Settings(&settings))) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
    }

    const std::shared_ptr<CallbackState> state = callback_state_;
    webview_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri))) {
                    const bool allowed = is_allowed_document_url(uri);
                    CoTaskMemFree(uri);
                    if (!allowed) {
                        args->put_Cancel(TRUE);
                    }
                }
                return S_OK;
            })
            .Get(),
        &navigation_starting_token_);
    webview_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [state](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *) -> HRESULT {
                if (state->owner != nullptr && state->owner->window_ != nullptr) {
                    PostMessageW(state->owner->window_, kWebViewDocumentReadyMessage, 0U, 0L);
                }
                return S_OK;
            })
            .Get(),
        &navigation_completed_token_);
    webview_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                args->put_Handled(TRUE);
                return S_OK;
            })
            .Get(),
        &new_window_requested_token_);

    resize();
    controller_->put_IsVisible(TRUE);
    composition_device_->Commit();
    ready_ = true;
    PostMessageW(window_, kWebViewReadyMessage, 0U, 0L);
    return S_OK;
}

void WebViewHost::post_failure(HRESULT result) const noexcept
{
    if (window_ != nullptr) {
        PostMessageW(
            window_,
            kWebViewFailedMessage,
            static_cast<WPARAM>(static_cast<std::uint32_t>(result)),
            0L);
    }
}

void WebViewHost::apply_host_state() noexcept
{
    if (!ready_ || !webview_) {
        return;
    }

    std::wstring script = kOverlayBootstrapScript;
    script.append(L"\n;window.__chatviewApplyHostState && window.__chatviewApplyHostState({editing:");
    script.append(editing_ ? L"true" : L"false");
    script.append(L",status:");
    script.append(javascript_string(status_text_));
    script.append(L",tone:");
    script.append(javascript_string(status_tone_));
    script.append(L"});");
    webview_->ExecuteScript(script.c_str(), nullptr);
}

} // namespace chatview
