// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/native-chat-surface.hpp"
#include "hud/webview-host.hpp"
#include "native-chat-document.hpp"
#include <bcrypt.h>
#include <wrl/event.h>
#include <array>
#include <cwchar>
#include <iterator>

namespace chatview {
using Microsoft::WRL::Callback;
namespace {
constexpr size_t kMaxEnvelopeCharacters = 2U * 1024U * 1024U;
constexpr wchar_t kNonceMarker[] = L"__CHATVIEW_NATIVE_NONCE__";
bool matching_source(ICoreWebView2 *webview, const std::wstring &expected) noexcept
{
    LPWSTR source = nullptr;
    const bool valid = !expected.empty() && SUCCEEDED(webview->get_Source(&source)) &&
                       source != nullptr && expected == source;
    CoTaskMemFree(source);
    return valid;
}
}

struct NativeChatSurface::State {
    std::weak_ptr<WebViewHost::CallbackState> host;
    ICoreWebView2 *identity = nullptr;
    bool active = true;
    bool saw_navigation = false;
    bool loaded = false;
    bool handshake = false;
    UINT64 navigation = 0U;
    std::wstring document_url;
    std::wstring ready_message;
    std::wstring rendered_prefix;
    std::wstring invalid_message;
    unsigned long long frames = 0U;
    unsigned long long rejected = 0U;
    unsigned int messages = 0U;

    bool attached() const noexcept
    {
        const auto current = host.lock();
        return active && current && current->owner && current->owner->ready_ &&
               current->owner->webview_.Get() == identity &&
               current->owner->current_url_.empty() && !document_url.empty() &&
               current->owner->local_document_url_ == document_url;
    }
    void invalidate() noexcept
    {
        active = false; loaded = false; handshake = false; messages = 0U;
    }
};

NativeChatSurface::~NativeChatSurface() { close(); }

bool NativeChatSurface::open(WebViewHost &host) noexcept
{
    close();
    if (!host.ready_ || !host.webview_ || !host.callback_state_) return false;
    try {
        std::array<unsigned char, 16> bytes{};
        if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
        std::wstring nonce;
        constexpr wchar_t hex[] = L"0123456789abcdef";
        for (const auto byte : bytes) { nonce += hex[byte >> 4U]; nonce += hex[byte & 15U]; }
        std::wstring document(kNativeChatDocument);
        size_t position = 0U;
        while ((position = document.find(kNonceMarker, position)) != std::wstring::npos) {
            document.replace(position, std::size(kNonceMarker) - 1U, nonce);
            position += nonce.size();
        }
        auto state = std::make_shared<State>();
        state->host = host.callback_state_; state->identity = host.webview_.Get();
        state->ready_message = L"chat-ready:" + nonce;
        state->rendered_prefix = L"chat-rendered:" + nonce + L":";
        state->invalid_message = L"chat-invalid:" + nonce;
        state_ = state; webview_ = host.webview_;
        HRESULT result = webview_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [state](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                    if (!state->attached()) { state->invalidate(); return S_OK; }
                    BOOL cancelled = FALSE;
                    if (FAILED(args->get_Cancel(&cancelled))) { state->invalidate(); return S_OK; }
                    // The host may cancel a superseded queued navigation. That
                    // does not replace the current document or grant authority.
                    if (cancelled) return S_OK;
                    LPWSTR uri = nullptr;
                    const bool own = SUCCEEDED(args->get_Uri(&uri)) && uri && state->document_url == uri;
                    CoTaskMemFree(uri);
                    if (!own || state->saw_navigation || state->handshake ||
                        FAILED(args->get_NavigationId(&state->navigation))) {
                        state->invalidate();
                    } else {
                        state->saw_navigation = true; state->loaded = false;
                    }
                    // Never undo a cancellation by WebViewHost.
                    return S_OK;
                }).Get(), &navigation_token_);
        if (FAILED(result)) { close(); return false; }
        result = webview_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [state](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                    UINT64 id = 0U; BOOL success = FALSE;
                    if (!state->attached() || !state->saw_navigation ||
                        FAILED(args->get_NavigationId(&id)) || id != state->navigation) return S_OK;
                    if (FAILED(args->get_IsSuccess(&success)) || !success) state->invalidate();
                    else state->loaded = true;
                    return S_OK;
                }).Get(), &completed_token_);
        if (FAILED(result)) { close(); return false; }
        result = webview_->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [state](ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *) -> HRESULT {
                    state->invalidate(); return S_OK;
                }).Get(), &process_token_);
        if (FAILED(result)) { close(); return false; }
        result = webview_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [state](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                    if (!state->attached() || !state->saw_navigation) return S_OK;
                    LPWSTR source = nullptr;
                    const bool own = SUCCEEDED(args->get_Source(&source)) && source && state->document_url == source;
                    CoTaskMemFree(source);
                    if (!own) return S_OK;
                    LPWSTR message = nullptr;
                    if (FAILED(args->TryGetWebMessageAsString(&message)) || !message) { CoTaskMemFree(message); return S_OK; }
                    if (wcscmp(message, state->ready_message.c_str()) == 0) state->handshake = true;
                    else if (state->handshake && wcscmp(message, state->invalid_message.c_str()) == 0) {
                        ++state->rejected; state->messages = 0U;
                    } else if (state->handshake && wcsncmp(message, state->rendered_prefix.c_str(), state->rendered_prefix.size()) == 0) {
                        const wchar_t *digits = message + state->rendered_prefix.size();
                        unsigned int count = 0U; bool valid = *digits != L'\0';
                        unsigned int length = 0U;
                        for (; *digits && valid; ++digits) {
                            valid = ++length <= 3U && *digits >= L'0' && *digits <= L'9';
                            if (valid) count = count * 10U + static_cast<unsigned int>(*digits - L'0');
                        }
                        if (valid && count <= 100U) { ++state->frames; state->messages = count; }
                    }
                    CoTaskMemFree(message);
                    return S_OK;
                }).Get(), &message_token_);
        if (FAILED(result) || !host.navigate_local_document(document.c_str(), state->document_url)) {
            close(); return false;
        }
        return true;
    } catch (...) { close(); return false; }
}

bool NativeChatSurface::ready() const noexcept
{
    return state_ && state_->attached() && state_->loaded && state_->handshake;
}

bool NativeChatSurface::publish(const std::wstring &envelope) noexcept
{
    if (!ready() || envelope.empty() || envelope.size() > kMaxEnvelopeCharacters ||
        envelope.find(L'\0') != std::wstring::npos || !matching_source(webview_.Get(), state_->document_url)) return false;
    // The document only accepts the versioned chat-snapshot display DTO.
    // PostWebMessageAsJson validates JSON; data is never ExecuteScript source.
    return SUCCEEDED(webview_->PostWebMessageAsJson(envelope.c_str()));
}

void NativeChatSurface::close() noexcept
{
    const auto host = state_ ? state_->host.lock() : nullptr;
    const bool clear_document = state_ && state_->attached() && webview_ &&
                                matching_source(webview_.Get(), state_->document_url);
    if (state_) state_->invalidate();
    if (webview_) {
        if (navigation_token_.value) webview_->remove_NavigationStarting(navigation_token_);
        if (completed_token_.value) webview_->remove_NavigationCompleted(completed_token_);
        if (message_token_.value) webview_->remove_WebMessageReceived(message_token_);
        if (process_token_.value) webview_->remove_ProcessFailed(process_token_);
        if (clear_document && host && host->owner) {
            std::wstring document_url;
            if (!host->owner->navigate_local_document(
                    L"<!doctype html><meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'\"><body></body>",
                    document_url)) host->owner->post_failure(E_FAIL);
        }
    }
    navigation_token_ = {}; completed_token_ = {}; message_token_ = {}; process_token_ = {};
    state_.reset(); webview_.Reset();
}
unsigned long long NativeChatSurface::rendered_frames() const noexcept { return state_ ? state_->frames : 0U; }
unsigned int NativeChatSurface::rendered_messages() const noexcept { return state_ ? state_->messages : 0U; }
unsigned long long NativeChatSurface::rejected_frames() const noexcept { return state_ ? state_->rejected : 0U; }
}
