// SPDX-License-Identifier: GPL-2.0-or-later
// Production DirectComposition/WebView2 host; all messages here are synthetic.
#include "hud/native-chat-surface.hpp"
#include "hud/webview-host.hpp"
#include <Windows.h>
#include <wrl/event.h>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace chatview {
struct NativeChatSurfaceTestAccess {
    static ICoreWebView2 *core(NativeChatSurface &surface) { return surface.webview_.Get(); }
    static bool omit_completed_for_current_document(NativeChatSurface &surface) {
        if (!surface.webview_ || !surface.completed_token_.value ||
            FAILED(surface.webview_->remove_NavigationCompleted(surface.completed_token_))) return false;
        surface.completed_token_ = {};
        return true;
    }
};
}

namespace {
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}
void expect(bool value, const char *label)
{
    if (!value) throw std::runtime_error(label);
}
void await(const std::function<bool()> &predicate, const char *label)
{
    const ULONGLONG deadline = GetTickCount64() + 15000U;
    while (!predicate()) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (GetTickCount64() >= deadline) throw std::runtime_error(label);
        MsgWaitForMultipleObjectsEx(0U, nullptr, 10U, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
std::wstring source(ICoreWebView2 *core)
{
    LPWSTR uri = nullptr;
    const HRESULT result = core->get_Source(&uri);
    const std::wstring value = uri ? uri : L"";
    CoTaskMemFree(uri);
    expect(SUCCEEDED(result), "read document identity");
    return value;
}
std::wstring evaluate(ICoreWebView2 *core, const wchar_t *script)
{
    struct Result { bool done = false; HRESULT status = E_FAIL; std::wstring json; };
    const auto state = std::make_shared<Result>();
    expect(SUCCEEDED(core->ExecuteScript(script,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [state](HRESULT result, LPCWSTR json) -> HRESULT {
                state->status = result;
                if (json) state->json = json;
                state->done = true;
                return S_OK;
            }).Get())), "queue fixed test DOM inspection");
    await([&] { return state->done; }, "DOM inspection callback");
    expect(SUCCEEDED(state->status), "DOM inspection succeeded");
    return state->json;
}
void expect_cancelled(ICoreWebView2 *core, const std::wstring &uri)
{
    struct Result { bool started = false; bool cancelled = false; };
    const auto state = std::make_shared<Result>();
    EventRegistrationToken token{};
    expect(SUCCEEDED(core->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [state, uri](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                LPWSTR target = nullptr;
                const bool matches = SUCCEEDED(args->get_Uri(&target)) && target && uri == target;
                CoTaskMemFree(target);
                if (matches) {
                    BOOL cancel = FALSE;
                    state->cancelled = SUCCEEDED(args->get_Cancel(&cancel)) && cancel;
                    state->started = true;
                }
                return S_OK;
            }).Get(), &token)), "register negative navigation assertion");
    const auto result = core->Navigate(uri.c_str());
    try {
        expect(SUCCEEDED(result), "queue forbidden navigation for policy test");
        await([&] { return state->started; }, "forbidden navigation observed");
    } catch (...) {
        core->remove_NavigationStarting(token);
        throw;
    }
    core->remove_NavigationStarting(token);
    expect(state->cancelled, "host cancels unowned document");
}
constexpr wchar_t kMessage[] = LR"JSON({"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":1,"messages":[{"nickname":"모의 사용자 <script>","content":"한글 😀 <img onerror=evil()>","messageTime":1700000000000}]}})JSON";
}
int main()
{
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary)) return 2;
    const std::wstring profile = std::wstring(temporary) + L"ChatView-NativeChat-" + std::to_wstring(GetCurrentProcessId());
    if (!CreateDirectoryW(profile.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return 3;
    SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str());
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 4;
    int code = 0;
    HWND window = nullptr;
    try {
        WNDCLASSW klass{}; klass.lpfnWndProc = window_proc;
        klass.hInstance = GetModuleHandleW(nullptr); klass.lpszClassName = L"ChatView.NativeChat.Test";
        expect(RegisterClassW(&klass) != 0U, "window class");
        window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, klass.lpszClassName,
            L"ChatView synthetic native renderer test", WS_OVERLAPPEDWINDOW,
            30, 30, 500, 640, nullptr, nullptr, klass.hInstance, nullptr);
        expect(window != nullptr, "window creation"); ShowWindow(window, SW_SHOWNOACTIVATE);
        chatview::WebViewHost host;
        chatview::NativeChatSurface surface;
        expect(!surface.open(host), "uninitialized host rejected");
        expect(!surface.publish(kMessage), "pre-handshake publish rejected");
        expect(host.initialize(window), "initialize production host");
        await([&] { return host.ready(); }, "WebView2 startup");
        expect(surface.open(host), "open embedded own chat");
        // Reproduce missing navigation-completion notifications on REAL WebView2,
        // with no DOMContentLoaded dependency. Only the owned document's
        // exact source, navigation ID and nonce message establish readiness.
        expect(chatview::NativeChatSurfaceTestAccess::omit_completed_for_current_document(surface),
            "omit first native NavigationCompleted notification");
        await([&] { return surface.ready(); }, "owned document nonce handshake without load events");
        ComPtr<ICoreWebView2> core = chatview::NativeChatSurfaceTestAccess::core(surface);
        const std::wstring first_url = source(core.Get());
        expect(first_url.starts_with(L"https://chatview.invalid/"), "host-owned memory document");
        // Capture only the synthetic document's own nonce in this test. No
        // provider tokens or private chat text are logged or persisted.
        const std::wstring first_nonce_json = evaluate(core.Get(),
            L"document.querySelector('script[type=module]')?.nonce");
        expect(first_nonce_json.size() == 34U && first_nonce_json.front() == L'"' &&
            first_nonce_json.back() == L'"', "first document has one 32-byte-hex nonce");
        const std::wstring old_nonce = first_nonce_json.substr(1U, 32U);
        expect(old_nonce.find_first_not_of(L"0123456789abcdef") == std::wstring::npos,
            "first private nonce is lowercase hexadecimal");
        // The combined host bootstrap must still install the existing
        // closed-shadow overlay on the real first WebView2 document.
        await([&] { return evaluate(core.Get(), LR"JS(
            (() => {
              const root = document.querySelector('chatview-private-hud-root');
              return Boolean(root && root.parentNode === document.documentElement &&
                root.shadowRoot === null);
            })()
        )JS") == L"true"; }, "host bootstrap installs the existing private overlay");
        expect(evaluate(core.Get(), LR"JS(
            globalThis.nativeMessageContract = null;
            window.chrome.webview.addEventListener('message', event => {
                if (event.data?.type === 'chat-snapshot') {
                    globalThis.nativeMessageContract = {
                        sourceMatches: event.source === window.chrome.webview,
                        isTrusted: Boolean(event.isTrusted)
                    };
                }
            });
            true;
        )JS") == L"true", "observe documented native bridge contract");
        expect(surface.publish(kMessage), "publish synthetic Unicode message");
        await([&] { return evaluate(core.Get(), L"nativeMessageContract !== null") == L"true"; }, "native bridge event delivered");
        std::wcout << L"Native bridge contract: " << evaluate(core.Get(), L"nativeMessageContract") << L'\n';
        expect(evaluate(core.Get(), L"nativeMessageContract.sourceMatches") == L"true", "native event identifies its bridge");
        await([&] { return surface.rendered_frames() == 1U; }, "actual DOM render acknowledgement");
        expect(surface.rendered_messages() == 1U, "one rendered text row");
        expect(evaluate(core.Get(), LR"JS(
            document.querySelector('#messages li b').textContent === '모의 사용자 <script>' &&
            document.querySelector('#messages li .content').textContent === '한글 😀 <img onerror=evil()>' &&
            document.querySelectorAll('#messages script, #messages img').length === 0
        )JS") == L"true", "Unicode preserved and markup inert in actual DOM");
        expect(!host.navigate(first_url), "private document cannot be entered through external settings");
        expect(!host.navigate(L"http://127.0.0.1:47831/chat"), "loopback settings remain prohibited");
        expect_cancelled(core.Get(), L"data:text/html,untrusted");
        expect_cancelled(core.Get(), L"https://chatview.invalid/unowned/index.html");
        expect_cancelled(core.Get(), L"about:blank");
        expect(surface.ready() && source(core.Get()) == first_url, "cancelled navigation does not gain or replace ownership");
        expect(surface.publish(LR"JSON({"type":"chat-snapshot","version":2,"snapshot":{}})JSON"), "queue unknown version");
        await([&] { return surface.rejected_frames() == 1U; }, "invalid version rejected in renderer");
        expect(surface.rendered_messages() == 0U, "bad data clears prior content");
        expect(!surface.publish(L"not JSON"), "malformed JSON rejected by WebView2");
        expect(!surface.publish(std::wstring(2U * 1024U * 1024U + 1U, L'x')), "oversized message rejected");
        std::wstring nul = kMessage; nul.push_back(L'\0');
        expect(!surface.publish(nul), "embedded NUL rejected");
        expect(surface.publish(kMessage), "restore chat");
        await([&] { return surface.rendered_frames() == 2U; }, "second render");
        expect(surface.publish(LR"JSON({"type":"chat-snapshot","version":1,"snapshot":{"state":"revoked","received":1,"messages":[]}})JSON"), "revoke frame");
        await([&] { return surface.rendered_frames() == 3U; }, "revoke applied");
        expect(surface.rendered_messages() == 0U, "revoke clears DOM");
        const auto setup_navigations = std::make_shared<unsigned int>(0U);
        EventRegistrationToken setup_navigation_token{};
        expect(SUCCEEDED(core->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [setup_navigations](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *) -> HRESULT {
                    ++*setup_navigations;
                    return S_OK;
                }).Get(), &setup_navigation_token)), "observe setup navigation churn");
        for (unsigned int request = 0U; request < 12U; ++request) host.show_setup_page();
        expect(!surface.ready() && !surface.publish(kMessage), "setup immediately invalidates private delivery");
        await([&] { return evaluate(core.Get(), L"document.querySelector('h1')?.textContent === 'ChatView needs a chat URL'") == L"true"; }, "setup page actually loads");
        const std::wstring setup_url = source(core.Get());
        const std::wstring setup_history = evaluate(core.Get(), L"history.length");
        expect(evaluate(core.Get(), L"globalThis.setupCanary = true") == L"true", "setup document canary");
        for (unsigned int request = 0U; request < 12U; ++request) {
            host.show_setup_page();
            expect(evaluate(core.Get(), L"globalThis.setupCanary === true") == L"true", "setup DOM survives repeated requests");
            expect(source(core.Get()) == setup_url, "setup identity is stable");
        }
        core->remove_NavigationStarting(setup_navigation_token);
        expect(*setup_navigations == 1U, "one setup navigation for pending and loaded requests");
        expect(evaluate(core.Get(), L"history.length") == setup_history, "setup notifications do not grow navigation history");
        expect(host.reload(), "explicit recovery still reloads setup");
        await([&] { return source(core.Get()) != setup_url &&
            evaluate(core.Get(), L"document.querySelector('h1')?.textContent === 'ChatView needs a chat URL'") == L"true"; }, "fresh setup after explicit recovery");
        expect(evaluate(core.Get(), L"globalThis.setupCanary === undefined") == L"true", "explicit recovery replaces the document");
        const std::wstring reloaded_setup = source(core.Get());
        host.show_setup_page();
        expect(evaluate(core.Get(), L"globalThis.setupCanary === undefined") == L"true" &&
            source(core.Get()) == reloaded_setup, "recovered setup remains idempotent");
        surface.close(); surface.close();
        expect(surface.open(host), "new own document after close");
        await([&] { return surface.ready(); }, "fresh document handshake");
        expect(source(core.Get()) != first_url, "reopen gets distinct document identity");
        const std::wstring fresh_nonce_json = evaluate(core.Get(),
            L"document.querySelector('script[type=module]')?.nonce");
        expect(fresh_nonce_json.size() == 34U &&
            fresh_nonce_json.substr(1U, 32U) != old_nonce,
            "reopened private document has a different nonce");
        const std::wstring current_nonce = fresh_nonce_json.substr(1U, 32U);
        expect_cancelled(core.Get(), first_url);
        expect(surface.ready() && surface.rendered_frames() == 0U &&
            surface.rendered_messages() == 0U,
            "a new document starts with no previous render acknowledgements");

        // Replay an old document's rendered ACK through the CURRENT WebView2
        // JS bridge (an even stronger adversarial case than an old source URL).
        // A separate test observer is the delivery barrier: the production
        // handler has already seen this old-nonce message before we assert.
        const std::wstring old_ack = L"chat-rendered:" + old_nonce + L":1:1";
        const auto observed_old_ack = std::make_shared<bool>(false);
        EventRegistrationToken observe_token{};
        expect(SUCCEEDED(core->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [old_ack, observed_old_ack](
                    ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                    LPWSTR raw = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) &&
                        raw && old_ack == raw) *observed_old_ack = true;
                    CoTaskMemFree(raw);
                    return S_OK;
                }).Get(), &observe_token)), "observe stale nonce injection");
        const std::wstring inject = L"window.chrome.webview.postMessage('" +
            old_ack + L"'); true";
        expect(evaluate(core.Get(), inject.c_str()) == L"true",
            "inject prior document's obsolete renderer ACK");
        await([&] { return *observed_old_ack; }, "old ACK delivered to native handlers");
        core->remove_WebMessageReceived(observe_token);
        expect(surface.ready() && surface.rendered_frames() == 0U &&
            surface.rendered_messages() == 0U,
            "old document ACK cannot count as the new document's first text");
        expect(surface.publish(kMessage), "reopened render");
        await([&] { return surface.rendered_frames() == 1U; }, "fresh counters");
        expect(surface.rendered_messages() == 1U,
            "only the current document's actual Unicode text counts");
        // The nonce remains valid across frames of one document. Previously a
        // delayed "count=1" acknowledgement could satisfy a new publication.
        // Exact frame IDs now prevent duplicates and prior-frame ACKs from
        // advancing the native first-text milestone, even under forged input.
        const std::wstring stale_frame_ack = L"chat-rendered:" + current_nonce + L":1:1";
        const auto stale_seen = std::make_shared<unsigned int>(0U);
        EventRegistrationToken stale_token{};
        expect(SUCCEEDED(core->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [stale_frame_ack, stale_seen](
                    ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                    LPWSTR raw = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) &&
                        raw && stale_frame_ack == raw) ++*stale_seen;
                    CoTaskMemFree(raw);
                    return S_OK;
                }).Get(), &stale_token)), "observe a duplicate ACK inside this owned document");
        const std::wstring inject_same = L"window.chrome.webview.postMessage('" +
            stale_frame_ack + L"'); true";
        expect(evaluate(core.Get(), inject_same.c_str()) == L"true", "send exact old-frame ACK");
        await([&] { return *stale_seen == 1U; }, "duplicate same-document ACK delivered");
        expect(surface.rendered_frames() == 1U && surface.rendered_messages() == 1U,
            "duplicate ID cannot satisfy a new native render");
        expect(surface.publish(kMessage), "publish another real frame in the same document");
        await([&] { return surface.rendered_frames() == 2U; }, "new frame ID acknowledged");
        expect(evaluate(core.Get(), inject_same.c_str()) == L"true", "replay earlier frame ID");
        await([&] { return *stale_seen == 2U; }, "older ACK delivered after fresh render");
        core->remove_WebMessageReceived(stale_token);
        expect(surface.rendered_frames() == 2U && surface.rendered_messages() == 1U,
            "previous frame ACK cannot count as the new current frame");
        surface.close();
        // Clear and open can be queued together; an old cancelled event must
        // neither authorize its stale document nor invalidate the newer one.
        expect(surface.open(host), "immediate reopen while clear is queued");
        await([&] { return surface.ready(); }, "queued clear followed by fresh handshake");
        // Each case supersedes two pending setup navigations without pumping
        // between requests. No retry-on-failure or relaxed DOM/time bounds.
        for (unsigned transition = 0; transition < 4; ++transition) {
            const std::wstring previous_owned_url = source(core.Get());
            host.show_setup_page();
            expect(host.reload(), "queue prior setup replacement");
            expect(surface.open(host), "private chat supersedes pending setup");
            await([&] { return surface.ready(); }, "new owned document after cancelled setup");
            expect(source(core.Get()) != previous_owned_url,
                "the pending old navigation cannot authorize or replace the new owned URL");
            expect(surface.publish(kMessage), "first message after superseded setup");
            await([&] { return surface.rendered_frames() == 1U && surface.rendered_messages() == 1U; },
                "first real render after superseded setup");
            expect(evaluate(core.Get(), L"document.querySelectorAll('#messages li').length === 1") == L"true",
                "first text row exists in the latest owned DOM");
        }
        // The shared browser/native renderer should not destroy every DOM
        // message when the service repeats the same snapshot on its heartbeat.
        // The exact ACK is still sent for each delivery, even if no DOM node
        // needs to change. A new message should only append its own row.
        expect(evaluate(core.Get(), L"globalThis.unchangedFirstRow = document.querySelector('#messages li'); !!globalThis.unchangedFirstRow") == L"true",
            "capture the first owned chat DOM row before the repeated heartbeat");
        expect(surface.publish(kMessage), "repeat unchanged display heartbeat");
        await([&] { return surface.rendered_frames() == 2U; }, "heartbeat receives its own distinct frame acknowledgement");
        expect(evaluate(core.Get(), L"document.querySelector('#messages li') === globalThis.unchangedFirstRow") == L"true",
            "unchanged heartbeat preserves actual WebView2 text row identity");
        constexpr wchar_t kSecondText[] = LR"JSON({"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":2,"messages":[{"nickname":"모의 사용자 <script>","content":"한글 😀 <img onerror=evil()>","messageTime":1700000000000},{"nickname":"추가 채팅","content":"두 번째 메시지","messageTime":1700000000001}]}})JSON";
        expect(surface.publish(kSecondText), "new subscribed chat appends on the owned DOM");
        await([&] { return surface.rendered_frames() == 3U && surface.rendered_messages() == 2U; },
            "incremental second row gets an exact native frame acknowledgement");
        expect(evaluate(core.Get(), LR"JS(
            document.querySelectorAll('#messages li').length === 2 &&
            document.querySelector('#messages li') === globalThis.unchangedFirstRow &&
            document.querySelectorAll('#messages li')[1].querySelector('.content').textContent === '두 번째 메시지'
        )JS") == L"true", "the old visible chat row survives append without HTML injection");
        expect(host.navigate(L"https://www.youtube.com/live_chat?is_popout=1&v=abcdefghijk"), "existing external navigation policy remains usable");
        expect(!surface.publish(kMessage), "external page cannot receive chat");
        surface.close(); host.close(); core.Reset();
        expect(!surface.publish(kMessage), "closed host cannot receive chat");
        expect(host.initialize(window), "host can be recreated");
        await([&] { return host.ready(); }, "recreated host ready");
        expect(surface.open(host), "surface attaches to recreated host");
        await([&] { return surface.ready(); }, "recreated surface ready");
        host.close(); expect(!surface.ready(), "host destruction invalidates surface"); surface.close();
        std::cout << "Native own-chat DOM, memory navigation isolation, setup and teardown passed\n";
    } catch (const std::exception &error) {
        std::cerr << "Native chat test failed: " << error.what() << '\n'; code = 1;
    }
    if (window) DestroyWindow(window);
    CoUninitialize();
    return code;
}
