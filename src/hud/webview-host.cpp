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
#include <cwchar>
#include <string>

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
<p>Paste a Weflab page or a CHZZK, SOOP, or YouTube broadcast/chat URL, then save.</p>
<p><code>Ctrl + Alt + Shift + H</code> unlocks this overlay for moving and resizing.</p>
</main>
</body>
</html>
)HTML";

constexpr wchar_t kOverlayBootstrapScript[] = LR"JS(
(() => {
  if (window.top !== window || window.__chatviewBootstrapInstalled) return;
  window.__chatviewBootstrapInstalled = true;
  window.__chatviewPendingState = { editing: false, status: '', tone: '#aeb0b2' };

  const apply = () => {
    const view = window.__chatviewNative;
    const state = window.__chatviewPendingState;
    if (!view || !state) return;
    view.host.dataset.editing = state.editing ? '1' : '0';
    view.host.dataset.hasStatus = state.status ? '1' : '0';
    view.host.style.setProperty('--tone', state.tone || '#aeb0b2');
    view.statusText.textContent = state.status || '';
  };

  const install = () => {
    try {
      if (!document.documentElement) {
        setTimeout(install, 0);
        return;
      }

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
      if (!host || !window.__chatviewNative) {
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

      if (!window.__chatviewHostObserver) {
        window.__chatviewHostObserver = new MutationObserver(() => {
          const view = window.__chatviewNative;
          if (view && !view.host.isConnected && document.documentElement) {
            document.documentElement.appendChild(view.host);
          }
        });
        window.__chatviewHostObserver.observe(document.documentElement, { childList: true });
      }
      apply();
    } catch (_) {
      setTimeout(install, 50);
    }
  };

  window.__chatviewEnsureHost = install;
  window.__chatviewApplyHostState = (state) => {
    window.__chatviewPendingState = state || window.__chatviewPendingState;
    install();
    apply();
  };
  install();
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

WebViewHost::WebViewHost()
    : callback_state_(std::make_shared<CallbackState>())
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

    SetEnvironmentVariableW(
        L"WEBVIEW2_DEFAULT_BACKGROUND_COLOR", L"00000000");

    const std::shared_ptr<CallbackState> state = callback_state_;
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        user_data_folder.c_str(),
        nullptr,
        Callback<
            ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [state](
                HRESULT callback_result,
                ICoreWebView2Environment *environment) -> HRESULT {
                return state->owner != nullptr
                           ? state->owner->on_environment_created(
                                 callback_result, environment)
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
    current_url_.clear();
    if (webview4_ && download_starting_token_.value != 0) {
        webview4_->remove_DownloadStarting(download_starting_token_);
    }
    if (webview_) {
        if (navigation_starting_token_.value != 0) {
            webview_->remove_NavigationStarting(navigation_starting_token_);
        }
        if (navigation_completed_token_.value != 0) {
            webview_->remove_NavigationCompleted(
                navigation_completed_token_);
        }
        if (new_window_requested_token_.value != 0) {
            webview_->remove_NewWindowRequested(
                new_window_requested_token_);
        }
        if (permission_requested_token_.value != 0) {
            webview_->remove_PermissionRequested(
                permission_requested_token_);
        }
        if (process_failed_token_.value != 0) {
            webview_->remove_ProcessFailed(process_failed_token_);
        }
    }

    navigation_starting_token_ = {};
    navigation_completed_token_ = {};
    new_window_requested_token_ = {};
    permission_requested_token_ = {};
    process_failed_token_ = {};
    download_starting_token_ = {};

    if (controller_) {
        controller_->Close();
    }
    webview4_.Reset();
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
    if (!ready_ || !webview_) {
        return false;
    }

    const std::wstring normalized = normalize_chat_url(url);
    if (normalized.empty()) {
        return false;
    }

    current_url_ = normalized;
    return SUCCEEDED(webview_->Navigate(current_url_.c_str()));
}

bool WebViewHost::reload() noexcept
{
    if (!ready_ || !webview_) {
        return false;
    }
    if (current_url_.empty()) {
        return SUCCEEDED(webview_->NavigateToString(kSetupPage));
    }
    return SUCCEEDED(webview_->Navigate(current_url_.c_str()));
}

void WebViewHost::show_setup_page() noexcept
{
    current_url_.clear();
    if (ready_ && webview_) {
        const HRESULT result = webview_->NavigateToString(kSetupPage);
        if (FAILED(result)) {
            post_failure(result);
        }
    }
}

void WebViewHost::set_host_state(
    bool editing,
    const std::wstring &status_text,
    const std::wstring &status_tone) noexcept
{
    editing_ = editing;
    status_text_ = status_text;
    status_tone_ = status_tone;
    apply_host_state();
}

bool WebViewHost::forward_mouse_message(
    UINT message, WPARAM wparam, LPARAM lparam) noexcept
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
            static_cast<std::int32_t>(
                GET_WHEEL_DELTA_WPARAM(wparam)));
    } else if (message == WM_XBUTTONDOWN ||
               message == WM_XBUTTONUP ||
               message == WM_XBUTTONDBLCLK) {
        mouse_data =
            static_cast<UINT32>(GET_XBUTTON_WPARAM(wparam));
    }

    const auto event_kind =
        static_cast<COREWEBVIEW2_MOUSE_EVENT_KIND>(message);
    const auto virtual_keys =
        static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(
            GET_KEYSTATE_WPARAM(wparam));
    return SUCCEEDED(composition_controller_->SendMouseInput(
        event_kind, virtual_keys, mouse_data, point));
}

void WebViewHost::focus() noexcept
{
    if (window_ != nullptr) {
        SetFocus(window_);
    }
    if (controller_) {
        controller_->MoveFocus(
            COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
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
    HRESULT result,
    ICoreWebView2Environment *environment) noexcept
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
        Callback<
            ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
            [state](
                HRESULT callback_result,
                ICoreWebView2CompositionController *controller) -> HRESULT {
                return state->owner != nullptr
                           ? state->owner->on_controller_created(
                                 callback_result, controller)
                           : S_OK;
            })
            .Get());
    if (FAILED(result)) {
        post_failure(result);
    }
    return S_OK;
}

HRESULT WebViewHost::on_controller_created(
    HRESULT result,
    ICoreWebView2CompositionController *controller) noexcept
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

    result = webview_.As(&webview4_);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    ComPtr<ICoreWebView2_8> webview8;
    result = webview_.As(&webview8);
    if (FAILED(result) || FAILED(webview8->put_IsMuted(TRUE))) {
        post_failure(FAILED(result) ? result : E_FAIL);
        return S_OK;
    }

    result = composition_controller_->put_RootVisualTarget(
        root_visual_.Get());
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    ComPtr<ICoreWebView2Controller2> controller2;
    result = controller_.As(&controller2);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    COREWEBVIEW2_COLOR transparent{};
    result = controller2->put_DefaultBackgroundColor(transparent);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    ComPtr<ICoreWebView2Controller4> controller4;
    result = controller_.As(&controller4);
    if (FAILED(result) ||
        FAILED(controller4->put_AllowExternalDrop(FALSE))) {
        post_failure(FAILED(result) ? result : E_FAIL);
        return S_OK;
    }

    ComPtr<ICoreWebView2Settings> settings;
    result = webview_->get_Settings(&settings);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }
    if (FAILED(settings->put_AreDefaultContextMenusEnabled(FALSE)) ||
        FAILED(settings->put_AreDefaultScriptDialogsEnabled(FALSE)) ||
        FAILED(settings->put_IsBuiltInErrorPageEnabled(FALSE)) ||
        FAILED(settings->put_IsStatusBarEnabled(FALSE)) ||
        FAILED(settings->put_AreDevToolsEnabled(FALSE)) ||
        FAILED(settings->put_IsZoomControlEnabled(FALSE)) ||
        FAILED(settings->put_IsWebMessageEnabled(FALSE)) ||
        FAILED(settings->put_AreHostObjectsAllowed(FALSE))) {
        post_failure(E_FAIL);
        return S_OK;
    }

    ComPtr<ICoreWebView2Settings3> settings3;
    result = settings.As(&settings3);
    if (FAILED(result) ||
        FAILED(settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE))) {
        post_failure(FAILED(result) ? result : E_FAIL);
        return S_OK;
    }

    const std::shared_ptr<CallbackState> state = callback_state_;
    result = webview_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [state](
                ICoreWebView2 *,
                ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                if (state->owner == nullptr) {
                    return args->put_Cancel(TRUE);
                }

                LPWSTR uri = nullptr;
                const HRESULT uri_result = args->get_Uri(&uri);
                if (FAILED(uri_result) || uri == nullptr) {
                    CoTaskMemFree(uri);
                    return args->put_Cancel(TRUE);
                }

                const bool allowed =
                    state->owner->is_navigation_allowed(uri);
                CoTaskMemFree(uri);
                return args->put_Cancel(allowed ? FALSE : TRUE);
            })
            .Get(),
        &navigation_starting_token_);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    result = webview_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [state](
                ICoreWebView2 *,
                ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                if (state->owner == nullptr ||
                    state->owner->window_ == nullptr) {
                    return S_OK;
                }

                BOOL succeeded = FALSE;
                HRESULT callback_result =
                    args->get_IsSuccess(&succeeded);
                if (FAILED(callback_result)) {
                    state->owner->post_failure(callback_result);
                    return S_OK;
                }

                if (succeeded) {
                    PostMessageW(
                        state->owner->window_,
                        kWebViewDocumentReadyMessage,
                        0U,
                        0L);
                    return S_OK;
                }

                COREWEBVIEW2_WEB_ERROR_STATUS status =
                    COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                callback_result = args->get_WebErrorStatus(&status);
                if (FAILED(callback_result)) {
                    state->owner->post_failure(callback_result);
                } else {
                    state->owner->post_navigation_failure(status);
                }
                return S_OK;
            })
            .Get(),
        &navigation_completed_token_);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    result = webview_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2 *,
               ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                return args->put_Handled(TRUE);
            })
            .Get(),
        &new_window_requested_token_);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    result = webview_->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [](ICoreWebView2 *,
               ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT {
                return args->put_State(
                    COREWEBVIEW2_PERMISSION_STATE_DENY);
            })
            .Get(),
        &permission_requested_token_);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    result = webview4_->add_DownloadStarting(
        Callback<ICoreWebView2DownloadStartingEventHandler>(
            [](ICoreWebView2 *,
               ICoreWebView2DownloadStartingEventArgs *args) -> HRESULT {
                const HRESULT cancel_result = args->put_Cancel(TRUE);
                if (FAILED(cancel_result)) {
                    return cancel_result;
                }
                return args->put_Handled(TRUE);
            })
            .Get(),
        &download_starting_token_);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    result = webview_->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [state](
                ICoreWebView2 *,
                ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
                if (state->owner == nullptr) {
                    return S_OK;
                }

                COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                const HRESULT callback_result =
                    args->get_ProcessFailedKind(&kind);
                if (FAILED(callback_result)) {
                    state->owner->post_failure(callback_result);
                } else {
                    state->owner->post_process_failure(kind);
                }
                return S_OK;
            })
            .Get(),
        &process_failed_token_);
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    result = webview_->AddScriptToExecuteOnDocumentCreated(
        kOverlayBootstrapScript,
        Callback<
            ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [state](HRESULT callback_result, LPCWSTR) -> HRESULT {
                return state->owner != nullptr
                           ? state->owner->on_bootstrap_registered(
                                 callback_result)
                           : S_OK;
            })
            .Get());
    if (FAILED(result)) {
        post_failure(result);
    }
    return S_OK;
}

HRESULT WebViewHost::on_bootstrap_registered(HRESULT result) noexcept
{
    if (FAILED(result)) {
        post_failure(result);
        return S_OK;
    }

    result = finish_controller_initialization();
    if (FAILED(result)) {
        post_failure(result);
    }
    return S_OK;
}

HRESULT WebViewHost::finish_controller_initialization() noexcept
{
    resize();

    HRESULT result = controller_->put_IsVisible(TRUE);
    if (FAILED(result)) {
        return result;
    }

    result = composition_device_->Commit();
    if (FAILED(result)) {
        return result;
    }

    ready_ = true;
    if (!PostMessageW(window_, kWebViewReadyMessage, 0U, 0L)) {
        ready_ = false;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

bool WebViewHost::is_navigation_allowed(
    const wchar_t *url) const noexcept
{
    if (url == nullptr) {
        return false;
    }

    if (wcscmp(url, L"about:blank") == 0) {
        return current_url_.empty();
    }
    if (current_url_.empty()) {
        return false;
    }

    return is_matching_chat_document_url(url, current_url_);
}

void WebViewHost::post_failure(HRESULT result) const noexcept
{
    if (window_ != nullptr) {
        PostMessageW(
            window_,
            kWebViewFailedMessage,
            static_cast<WPARAM>(
                static_cast<std::uint32_t>(result)),
            0L);
    }
}

void WebViewHost::post_process_failure(
    COREWEBVIEW2_PROCESS_FAILED_KIND kind) const noexcept
{
    if (window_ != nullptr) {
        PostMessageW(
            window_,
            kWebViewProcessFailedMessage,
            static_cast<WPARAM>(kind),
            0L);
    }
}

void WebViewHost::post_navigation_failure(
    COREWEBVIEW2_WEB_ERROR_STATUS status) const noexcept
{
    if (window_ != nullptr) {
        PostMessageW(
            window_,
            kWebViewNavigationFailedMessage,
            static_cast<WPARAM>(status),
            0L);
    }
}

void WebViewHost::apply_host_state() noexcept
{
    if (!ready_ || !webview_) {
        return;
    }

    std::wstring script =
        L"window.__chatviewEnsureHost && window.__chatviewEnsureHost();"
        L"window.__chatviewApplyHostState && "
        L"window.__chatviewApplyHostState({editing:";
    script.append(editing_ ? L"true" : L"false");
    script.append(L",status:");
    script.append(javascript_string(status_text_));
    script.append(L",tone:");
    script.append(javascript_string(status_tone_));
    script.append(L"});");
    webview_->ExecuteScript(script.c_str(), nullptr);
}

} // namespace chatview
