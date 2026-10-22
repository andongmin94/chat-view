// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/native-chat-surface.hpp"
#include "hud/webview-host.hpp"
#include "native-chat-document.hpp"
#include <bcrypt.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <wrl/event.h>
#include <array>
#include <cwchar>
#include <iterator>
#include <limits>

namespace chatview {
using Microsoft::WRL::Callback;
namespace {
constexpr size_t kMaxEnvelopeCharacters = 2U * 1024U * 1024U;
constexpr wchar_t kNonceMarker[] = L"__CHATVIEW_NATIVE_NONCE__";
bool decimal(const wchar_t *&text, wchar_t stop, unsigned int &result) noexcept
{
    const wchar_t *begin = text;
    unsigned int number = 0U;
    unsigned int length = 0U;
    for (; *text != stop && *text; ++text) {
        if (++length > 10U || *text < L'0' || *text > L'9') return false;
        const unsigned int digit = static_cast<unsigned int>(*text - L'0');
        if (number > (std::numeric_limits<unsigned int>::max() - digit) / 10U) return false;
        number = number * 10U + digit;
    }
    if (!length || *text != stop || (length > 1U && *begin == L'0')) return false;
    if (stop != L'\0') ++text;
    result = number; return true;
}
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
    // A source-scoped nonce handshake from the owned module proves the
    // parsed DOM is usable even if completion callbacks are delayed.
    bool handshake = false;
    UINT64 navigation = 0U;
    std::wstring document_url;
    std::wstring ready_message;
    std::wstring rendered_prefix;
    std::wstring invalid_prefix;
    unsigned int issued = 0U;
    unsigned int acknowledged = 0U;
    unsigned long long frames = 0U;
    unsigned long long rejected = 0U;
    unsigned int messages = 0U;

    // Being the currently selected native URL determines who must clear
    // potentially visible old pixels. Receiving authority is separate:
    // a failed navigation/process can invalidate the receiver while the host
    // is still showing the same formerly owned page.
    bool selected() const noexcept
    {
        const auto current = host.lock();
        return current && current->owner && current->owner->ready_ &&
               current->owner->webview_.Get() == identity &&
               current->owner->current_url_.empty() && !document_url.empty() &&
               current->owner->local_document_url_ == document_url;
    }
    bool attached() const noexcept { return active && selected(); }
    void invalidate() noexcept
    {
        active = false; handshake = false; messages = 0U;
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
        state->invalid_prefix = L"chat-invalid:" + nonce + L":";
        state_ = state; webview_ = host.webview_;
        HRESULT result = webview_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [state](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                    if (!state->active) return S_OK;
                    BOOL cancelled = FALSE;
                    if (FAILED(args->get_Cancel(&cancelled))) { state->invalidate(); return S_OK; }
                    // A host-cancelled old page cannot retire the new receiver.
                    if (cancelled) return S_OK;
                    LPWSTR uri = nullptr;
                    const bool own = SUCCEEDED(args->get_Uri(&uri)) && uri && state->document_url == uri;
                    CoTaskMemFree(uri);
                    // WebView2 may dispatch the superseded setup/clear
                    // NavigationStarting after Navigate() for this owned
                    // document already returned. The old synchronous
                    // "opening" flag was too short and could permanently
                    // poison this new receiver before its own navigation.
                    // The host independently cancels every URL except its
                    // reserved current document, and only an exact owned
                    // navigation ID plus nonce/source handshake grants ready.
                    // Ignore foreign starts ONLY until that owned start is
                    // seen. A later foreign/duplicate start still invalidates
                    // the receiver and can never deliver private text.
                    if (!own && !state->saw_navigation && !state->handshake)
                        return S_OK;
                    if (!state->attached() || !own || state->saw_navigation ||
                        state->handshake ||
                        FAILED(args->get_NavigationId(&state->navigation))) {
                        state->invalidate();
                    } else {
                        state->saw_navigation = true;
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
                    // An actual navigation failure revokes a previously
                    // accepted nonce handshake as well as any queued first text.
                    if (FAILED(args->get_IsSuccess(&success)) || !success) state->invalidate();
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
                    else if (state->handshake &&
                             wcsncmp(message, state->invalid_prefix.c_str(), state->invalid_prefix.size()) == 0) {
                        const wchar_t *digits = message + state->invalid_prefix.size();
                        unsigned int id = 0U;
                        if (decimal(digits, L'\0', id) && id == state->issued && id > state->acknowledged) {
                            state->acknowledged = id; ++state->rejected; state->messages = 0U;
                        }
                    } else if (state->handshake &&
                               wcsncmp(message, state->rendered_prefix.c_str(), state->rendered_prefix.size()) == 0) {
                        const wchar_t *digits = message + state->rendered_prefix.size();
                        unsigned int id = 0U, count = 0U;
                        if (decimal(digits, L':', id) && decimal(digits, L'\0', count) &&
                            id == state->issued && id > state->acknowledged && count <= 100U) {
                            state->acknowledged = id; ++state->frames; state->messages = count;
                        }
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
    return state_ && state_->attached() && state_->saw_navigation &&
           state_->handshake && webview_ &&
           matching_source(webview_.Get(), state_->document_url);
}

bool NativeChatSurface::discarded() const noexcept
{
    // A pending (but valid) asynchronous navigation is deliberately NOT
    // discarded: reopening it would introduce needless navigation churn.
    return !state_ || !state_->active;
}

bool NativeChatSurface::publish(const std::wstring &envelope) noexcept
{
    if (!ready() || envelope.empty() || envelope.size() > kMaxEnvelopeCharacters ||
        envelope.find(L'\0') != std::wstring::npos) return false;
    // Bind every DOM acknowledgement to this exact native delivery, not just
    // its message count. The server cannot supply or select the frame ID.
    // Neither the ID nor the chat text is interpolated into executable script.
    if (state_->issued == std::numeric_limits<unsigned int>::max()) return false;
    try {
        auto frame = winrt::Windows::Data::Json::JsonObject::Parse(envelope);
        const unsigned int id = state_->issued + 1U;
        frame.Insert(L"frameId", winrt::Windows::Data::Json::JsonValue::CreateStringValue(std::to_wstring(id)));
        const auto serialized = frame.Stringify();
        if (serialized.size() > kMaxEnvelopeCharacters) return false;
        // WebView2 can dispatch callbacks during COM calls. Install the ID
        // before posting, and roll back only if the call was rejected.
        state_->issued = id;
        if (FAILED(webview_->PostWebMessageAsJson(serialized.c_str()))) {
            state_->issued = id - 1U; return false;
        }
        return true;
    } catch (...) { return false; }
}

bool NativeChatSurface::jump_to_latest() noexcept
{
    // Never post to external, retired, unapproved or empty chat documents.
    // This UI-only command is not a chat frame and does not change issued,
    // acknowledged, rejected, or liveness counters.
    return ready() && rendered_messages() != 0U &&
           SUCCEEDED(webview_->PostWebMessageAsJson(
               LR"JSON({"type":"chat-jump-latest"})JSON"));
}

void NativeChatSurface::close() noexcept
{
    const auto host = state_ ? state_->host.lock() : nullptr;
    // A new own navigation may still be pending while WebView2 displays
    // the previous document's private chat. Clearing only when the visible
    // source matches this newest URL leaves the old text visible after Stop.
    // If this receiver still owns the selected local page, supersede ALL
    // pending own navigations with the fixed blank page, even if its nonce
    // handshake or source change has not completed yet. A failed receiver
    // may have active=false but still own VISIBLE private pixels; selected()
    // retains blanking responsibility without authorizing another message.
    // A deliberately selected external page fails selected() and is untouched.
    const bool clear_document = state_ && state_->selected() && webview_;
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
    navigation_token_ = {}; completed_token_ = {};
    message_token_ = {}; process_token_ = {};
    state_.reset(); webview_.Reset();
}
unsigned long long NativeChatSurface::rendered_frames() const noexcept { return state_ ? state_->frames : 0U; }
unsigned int NativeChatSurface::rendered_messages() const noexcept { return state_ ? state_->messages : 0U; }
unsigned long long NativeChatSurface::rejected_frames() const noexcept { return state_ ? state_->rejected : 0U; }
}
