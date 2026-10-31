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
struct WebViewHostTestAccess {
    static UINT64 navigation(const WebViewHost &host) { return host.current_navigation_id_; }
    static const std::wstring &local_document(const WebViewHost &host) { return host.local_document_url_; }
};
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
constexpr wchar_t kMessage[] = LR"JSON({"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":1,"messages":[{"nickname":"모의 사용자 <script>","content":"한글 😀 <img onerror=evil()>","messageTime":1700000000000,"userRoleCode":"streamer","verifiedMark":true}]}})JSON";
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
        expect(surface.discarded(), "an absent private document is explicitly unavailable");
        expect(!surface.publish(kMessage), "pre-handshake publish rejected");
        expect(!surface.jump_to_latest(), "locked latest shortcut refuses an unowned document");
        expect(host.initialize(window), "initialize production host");
        await([&] { return host.ready(); }, "WebView2 startup");
        expect(surface.open(host), "open embedded own chat");
        expect(!surface.discarded(),
            "a new own navigation is pending, not a reason to restart its 15-second budget");
        // Reproduce missing navigation-completion notifications on REAL WebView2,
        // with no DOMContentLoaded dependency. Only the owned document's
        // exact source, navigation ID and nonce message establish readiness.
        expect(chatview::NativeChatSurfaceTestAccess::omit_completed_for_current_document(surface),
            "omit first native NavigationCompleted notification");
        await([&] { return surface.ready(); }, "owned document nonce handshake without load events");
        expect(!surface.discarded(), "owned nonce-ready private document remains valid");
        const UINT64 first_navigation = chatview::WebViewHostTestAccess::navigation(host);
        expect(host.is_current_navigation(first_navigation),
            "owned first document is bound to a nonzero WebView2 navigation ID");
        ComPtr<ICoreWebView2> core = chatview::NativeChatSurfaceTestAccess::core(surface);
        const std::wstring first_url = source(core.Get());
        expect(first_url.starts_with(L"https://chatview.invalid/"), "host-owned memory document");
        expect(!surface.jump_to_latest(), "preapproved empty native document has no chat to navigate");
        // Capture only the synthetic document's own nonce in this test. No
        // provider tokens or private chat text are logged or persisted.
        const std::wstring first_nonce_json = evaluate(core.Get(),
            L"document.querySelector('script[nonce]:not([type])')?.nonce");
        expect(first_nonce_json.size() == 34U && first_nonce_json.front() == L'"' &&
            first_nonce_json.back() == L'"', "first document has one 32-byte-hex nonce");
        const std::wstring old_nonce = first_nonce_json.substr(1U, 32U);
        expect(old_nonce.find_first_not_of(L"0123456789abcdef") == std::wstring::npos,
            "first private nonce is lowercase hexadecimal");
        expect(evaluate(core.Get(), LR"JS((() => {
            const script = document.querySelector('script[nonce]:not([type])');
            return script && document.querySelectorAll('script[type=module]').length === 0 &&
                !script.textContent.includes('export function ') &&
                !script.textContent.includes('import ');
        })())JS") == L"true",
            "first native document uses one nonce-bound parser-executed script without imports or ESM exports");
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
        expect(evaluate(core.Get(), LR"JS(
            (() => {
                const row = document.querySelector('#messages li');
                const role = row?.querySelector('.chat-role.streamer');
                const verified = row?.querySelector('.chat-verified');
                return Boolean(role && verified &&
                    role.textContent === '스트리머' && verified.textContent === '인증' &&
                    getComputedStyle(role).display !== 'none' &&
                    row.querySelectorAll('img,script').length === 0);
            })()
        )JS") == L"true", "real native WebView2 displays inert streamer and verified badges");
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
        expect(!host.is_current_navigation(first_navigation),
            "superseding own chat immediately fences its prior navigation completion");
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
        expect(surface.discarded(), "closing the private document discards its exact owner");
        expect(surface.open(host), "new own document after close");
        await([&] { return surface.ready(); }, "fresh document handshake");
        const UINT64 fresh_navigation = chatview::WebViewHostTestAccess::navigation(host);
        expect(host.is_current_navigation(fresh_navigation) &&
            !host.is_current_navigation(first_navigation) && fresh_navigation != first_navigation,
            "fresh owned document replaces the old navigation ID as well as its nonce");
        expect(source(core.Get()) != first_url, "reopen gets distinct document identity");
        const std::wstring fresh_nonce_json = evaluate(core.Get(),
            L"document.querySelector('script[nonce]:not([type])')?.nonce");
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
        // Replacing an already owned private document twice without pumping
        // must not require Stop() or authorize either superseded nonce. The
        // latest permitted URL and exact first render are still required.
        const std::wstring superseded_owned_url = source(core.Get());
        expect(surface.open(host), "first rapid private document replacement");
        expect(surface.open(host), "second rapid private document replacement");
        await([&] { return surface.ready(); }, "latest private document survives rapid replacement");
        expect(source(core.Get()) != superseded_owned_url,
            "stale own document cannot remain the selected source");
        expect_cancelled(core.Get(), superseded_owned_url);
        expect(surface.publish(kMessage), "first message on latest rapidly selected document");
        await([&] { return surface.rendered_frames() == 1U && surface.rendered_messages() == 1U; },
            "latest own document receives its actual first text after rapid replacement");
        // Each case supersedes two pending setup navigations without pumping
        // between requests. No retry-on-failure or relaxed DOM/time bounds.
        for (unsigned transition = 0; transition < 4; ++transition) {
            const std::wstring previous_owned_url = source(core.Get());
            const UINT64 previous_navigation = chatview::WebViewHostTestAccess::navigation(host);
            host.show_setup_page();
            expect(!host.is_current_navigation(previous_navigation),
                "queued old success/failure cannot become current after setup selection");
            expect(host.reload(), "queue prior setup replacement");
            expect(surface.open(host), "private chat supersedes pending setup");
            await([&] { return surface.ready(); }, "new owned document after cancelled setup");
            expect(host.is_current_navigation(chatview::WebViewHostTestAccess::navigation(host)) &&
                !host.is_current_navigation(previous_navigation),
                "host acknowledges only the latest owned navigation after superseded setup");
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
        constexpr wchar_t kSecondText[] = LR"JSON({"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":2,"messages":[{"nickname":"모의 사용자 <script>","content":"한글 😀 <img onerror=evil()>","messageTime":1700000000000,"userRoleCode":"streamer","verifiedMark":true},{"nickname":"추가 채팅","content":"두 번째 메시지","messageTime":1700000000001}]}})JSON";
        expect(surface.publish(kSecondText), "new subscribed chat appends on the owned DOM");
        await([&] { return surface.rendered_frames() == 3U && surface.rendered_messages() == 2U; },
            "incremental second row gets an exact native frame acknowledgement");
        expect(evaluate(core.Get(), LR"JS(
            document.querySelectorAll('#messages li').length === 2 &&
            document.querySelector('#messages li') === globalThis.unchangedFirstRow &&
            document.querySelectorAll('#messages li')[1].querySelector('.content').textContent === '두 번째 메시지'
        )JS") == L"true", "the old visible chat row survives append without HTML injection");
        // Real owned WebView2: streamer-only reading controls must change
        // visual text/contrast, not replace chat nodes, authorize a stream or
        // interfere with exact native frame acknowledgements.
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            const previous = list.querySelector('li');
            const larger = document.getElementById('chat-font-larger');
            const contrast = document.getElementById('chat-contrast');
            if (!larger || !contrast) return false;
            larger.click();
            contrast.click();
            return list.style.fontSize === '18px' &&
                list.classList.contains('chat-high-contrast') &&
                contrast.getAttribute('aria-pressed') === 'true' &&
                list.querySelector('li') === previous &&
                list.querySelectorAll('li').length === 2;
        })())JS") == L"true",
            "native readability controls adjust only local text and contrast, preserving private rows");
        // Streamer UX: when reading older text, new provider messages should
        // add a bounded unread hint without stealing the scroll position.
        // This is the real immutable WebView2 renderer, not a browser mock.
        const auto history = [](int count, int first = 1, bool wrapped = false) {
            std::wstring json = LR"JSON({"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":)JSON";
            json += std::to_wstring(first + count - 1);
            json += LR"JSON(,"messages":[)JSON";
            for (int i = 0; i < count; ++i) {
                if (i) json += L",";
                json += LR"JSON({"nickname":"검증 사용자","content":"채팅 )JSON";
                json += std::to_wstring(first + i);
                if (wrapped) json += L" 너비가 바뀌면 여러 줄로 표시되는 합성 채팅 메시지입니다. 읽던 행의 위치를 확인합니다.";
                json += LR"JSON(","messageTime":)JSON";
                json += std::to_wstring(1700000000000LL + first + i - 1);
                json += L"}";
            }
            json += L"]}}";
            return json;
        };
        expect(surface.publish(history(8)), "fill own chat to exercise a scrolled-up history");
        await([&] { return surface.rendered_frames() == 4U &&
                            surface.rendered_messages() == 8U; }, "eight native messages rendered");
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            list.style.flex = '0 0 100px';
            list.style.height = '100px';
            return list.scrollHeight - list.clientHeight > 80 &&
                document.getElementById('jump-latest').hidden;
        })())JS") == L"true", "existing messages are scrollable without a false unread hint");
        await([&] { return evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            return list.clientHeight === 100 && list.scrollHeight - list.scrollTop - list.clientHeight <= 2;
        })())JS") == L"true"; }, "finish viewport resize before the reader scrolls into history");
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            list.scrollTop = 0;
            list.dispatchEvent(new Event('scroll'));
            return list.scrollTop === 0 && list.scrollHeight - list.clientHeight > 80;
        })())JS") == L"true", "reader deliberately scrolls after native layout settles");
        expect(surface.publish(history(9)), "deliver one genuinely new chat while reading history");
        await([&] { return surface.rendered_frames() == 5U &&
                            surface.rendered_messages() == 9U; }, "new native text and exact ACK");
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            const latest = document.getElementById('jump-latest');
            return !latest.hidden && latest.textContent.includes('새 채팅 1개') &&
                list.scrollTop === 0 && list.querySelectorAll('li').length === 9;
        })())JS") == L"true", "scrolled-up native HUD displays unread text without jumping");
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            const unread = document.getElementById('jump-latest');
            list.scrollTop = 90;
            if (list.scrollHeight - list.scrollTop - list.clientHeight < 80) return false;
            const anchor = [...list.children].find(row =>
                row.getBoundingClientRect().bottom > list.getBoundingClientRect().top);
            if (!anchor || anchor === list.firstElementChild) return false;
            const initial = anchor.getBoundingClientRect().top;
            const marker = unread.textContent;
            const font = list.style.fontSize;
            const larger = document.getElementById('chat-font-larger');
            const smaller = document.getElementById('chat-font-smaller');
            const contrast = document.getElementById('chat-contrast');
            larger.click();
            if (Math.abs(anchor.getBoundingClientRect().top - initial) > 2) return false;
            contrast.click();
            if (Math.abs(anchor.getBoundingClientRect().top - initial) > 2) return false;
            smaller.click();
            return Math.abs(anchor.getBoundingClientRect().top - initial) <= 2 &&
                list.style.fontSize === font && list.contains(anchor) &&
                !unread.hidden && unread.textContent === marker &&
                list.scrollHeight - list.scrollTop - list.clientHeight >= 80;
        })())JS") == L"true",
            "real private WebView2 anchors historical text through typography/contrast changes while unread remains");
        const auto frames_before_latest = surface.rendered_frames();
        expect(evaluate(core.Get(), LR"JS((() => {
            const latest = document.getElementById('jump-latest');
            return !latest.hidden && latest.getAttribute('aria-keyshortcuts') === 'Control+Alt+Shift+J' &&
                getComputedStyle(latest, '::after').content.includes('Ctrl+Alt+Shift+J');
        })())JS") == L"true", "native unread hint advertises the global latest shortcut");
        expect(surface.jump_to_latest(), "approved own WebView2 accepts the fixed latest-chat command");
        await([&] { return evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            const latest = document.getElementById('jump-latest');
            return latest.hidden && list.scrollHeight - list.scrollTop - list.clientHeight < 80;
        })())JS") == L"true"; }, "native latest shortcut scrolls and clears unread without unlocked pointer input");
        expect(surface.rendered_frames() == frames_before_latest && surface.rendered_messages() == 9U,
            "jump command does not create an ACK, provider message, or false chat receipt");
        // Exercise the 100-row rollover on the real WebView2 scroll container.
        // The user must keep reading the same surviving row whether the
        // browser applies scroll anchoring itself or not.
        expect(surface.publish(history(100)), "fill bounded native chat history");
        await([&] { return surface.rendered_frames() == 6U &&
                            surface.rendered_messages() == 100U; }, "100 native messages rendered");
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            const rows = list.children;
            list.scrollTop = rows[12].offsetTop - rows[0].offsetTop + 5;
            const anchor = [...rows].find(row =>
                row.getBoundingClientRect().bottom > list.getBoundingClientRect().top);
            if (!anchor || anchor === rows[0]) return false;
            globalThis.rolloverAnchor = anchor;
            globalThis.rolloverTop = anchor.getBoundingClientRect().top;
            return list.scrollHeight - list.scrollTop - list.clientHeight > 80;
        })())JS") == L"true", "reader scrolled inside 100-message native window");
        expect(surface.publish(history(100, 2)), "evict oldest native row while reading history");
        await([&] { return surface.rendered_frames() == 7U &&
                            surface.rendered_messages() == 100U; }, "rolling native message acknowledged");
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            const anchor = globalThis.rolloverAnchor;
            const hint = document.getElementById('jump-latest');
            return list.children.length === 100 &&
                list.contains(anchor) && !hint.hidden &&
                hint.textContent.includes('새 채팅 1개') &&
                list.firstElementChild.querySelector('.content').textContent === '채팅 2' &&
                list.lastElementChild.querySelector('.content').textContent === '채팅 101' &&
                Math.abs(anchor.getBoundingClientRect().top - globalThis.rolloverTop) <= 2;
        })())JS") == L"true", "real WebView2 rolling chat retains the same visible row and unread hint");
        // Resize the actual HWND and production composition controller. No new
        // delivery is needed to follow latest or retain a historical row.
        const auto resize_hud = [&](int width, int height) {
            expect(SetWindowPos(window, nullptr, 0, 0, width, height,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "resize native HUD window");
            host.resize();
        };
        resize_hud(560, 800);
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            list.style.removeProperty('flex');
            list.style.removeProperty('height');
            return true;
        })())JS") == L"true", "restore production flexible chat viewport");
        expect(surface.publish(history(100, 2, true)), "render wrapping text for native HUD resize");
        await([&] { return surface.rendered_frames() == 8U; }, "wrapping native history acknowledged");
        expect(surface.jump_to_latest(), "follow latest before native HUD resize");
        await([&] { return evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            if (list.clientHeight < 600 || list.scrollHeight - list.scrollTop - list.clientHeight > 2)
                return false;
            globalThis.resizeRows = [...list.children];
            globalThis.resizeHeight = list.clientHeight;
            return document.getElementById('jump-latest').hidden;
        })())JS") == L"true"; }, "latest visible in tall native HUD");
        const auto frames_before_resize = surface.rendered_frames();
        resize_hud(560, 400);
        await([&] { return evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            return globalThis.resizeHeight - list.clientHeight === 400 &&
                list.scrollHeight - list.scrollTop - list.clientHeight <= 2 &&
                document.getElementById('jump-latest').hidden &&
                [...list.children].every((row, i) => row === globalThis.resizeRows[i]);
        })())JS") == L"true"; }, "800-to-400 native HUD shrink follows latest without rebuilding rows");
        resize_hud(280, 400);
        await([&] { return evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            return list.clientWidth < 280 && list.scrollHeight - list.scrollTop - list.clientHeight <= 2 &&
                document.getElementById('jump-latest').hidden;
        })())JS") == L"true"; }, "native width reflow keeps latest visible");
        expect(surface.rendered_frames() == frames_before_resize,
            "height and width resize send no native frame acknowledgement");
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            list.scrollTop = list.children[12].offsetTop - list.children[0].offsetTop + 5;
            list.dispatchEvent(new Event('scroll'));
            globalThis.resizeAnchor = [...list.children].find(row =>
                row.getBoundingClientRect().bottom > list.getBoundingClientRect().top);
            globalThis.resizeOffset = globalThis.resizeAnchor.getBoundingClientRect().top -
                list.getBoundingClientRect().top;
            globalThis.resizeRowHeight = globalThis.resizeAnchor.getBoundingClientRect().height;
            return list.scrollHeight - list.scrollTop - list.clientHeight > 80;
        })())JS") == L"true", "read historical row in narrow native HUD");
        expect(surface.publish(history(100, 3, true)), "deliver new text while reading resized history");
        await([&] { return surface.rendered_frames() == 9U; }, "new historical delivery acknowledged once");
        resize_hud(560, 400);
        await([&] { return evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            const anchor = globalThis.resizeAnchor;
            const hint = document.getElementById('jump-latest');
            return list.clientWidth > 400 && list.contains(anchor) &&
                anchor.getBoundingClientRect().height < globalThis.resizeRowHeight &&
                Math.abs(anchor.getBoundingClientRect().top - list.getBoundingClientRect().top -
                    globalThis.resizeOffset) <= 2 && !hint.hidden &&
                hint.textContent.includes('새 채팅 1개') &&
                [...list.children].slice(0, -1).every((row, i) => row === globalThis.resizeRows[i + 1]);
        })())JS") == L"true"; }, "native width reflow preserves historical row, offset, unread and DOM identity");
        expect(surface.rendered_frames() == 9U && surface.rendered_messages() == 100U,
            "historical resize adds no ACK or message");
        expect(surface.publish(LR"JSON({"type":"chat-snapshot","version":1,"snapshot":{"state":"revoked","received":101,"messages":[]}})JSON"),
               "revoke clears the unread UI and private text");
        await([&] { return surface.rendered_frames() == 10U &&
                            surface.rendered_messages() == 0U; }, "revoked native HUD clears private text");
        expect(!surface.jump_to_latest(), "revoked local document cannot act on private chat navigation");
        expect(evaluate(core.Get(), LR"JS(
            document.getElementById('jump-latest').hidden &&
            document.querySelectorAll('#messages li').length === 0
        )JS") == L"true", "revocation clears unread hint and all previous native chat rows");
        expect(host.navigate(L"https://www.youtube.com/live_chat?is_popout=1&v=abcdefghijk"), "existing external navigation policy remains usable");
        expect(!surface.publish(kMessage), "external page cannot receive chat");
        surface.close();
        expect(chatview::WebViewHostTestAccess::local_document(host).empty(),
            "stopping private chat does not replace a deliberately selected external page");
        host.close(); core.Reset();
        expect(!surface.publish(kMessage), "closed host cannot receive chat");
        expect(host.initialize(window), "host can be recreated");
        await([&] { return host.ready(); }, "recreated host ready");
        expect(surface.open(host), "surface attaches to recreated host");
        await([&] { return surface.ready(); }, "recreated surface ready");
        core = chatview::NativeChatSurfaceTestAccess::core(surface);
        expect(surface.publish(kMessage), "render private chat before immediate Stop/replacement");
        await([&] { return surface.rendered_frames() == 1U &&
                            surface.rendered_messages() == 1U; },
            "actual private text reached owned DOM before its next navigation");
        const std::wstring original_source = source(core.Get());
        expect(surface.open(host), "queue a fresh owned document over the displayed private text");
        const std::wstring queued_source = chatview::WebViewHostTestAccess::local_document(host);
        expect(!queued_source.empty() && queued_source != original_source,
            "new pending own URL replaces the selected document without granting an ACK");
        // No message pump between selection and Stop. WebView2 can still
        // display the earlier private chat while the next nonce URL is queued.
        surface.close();
        const std::wstring cleared_source = chatview::WebViewHostTestAccess::local_document(host);
        expect(!cleared_source.empty() && cleared_source != queued_source &&
                   cleared_source != original_source,
            "Stop supersedes even an uncommitted own navigation with a fresh blank document");
        // WebView2 can update get_Source at NavigationStarting while the
        // previous document's DOM is still rendered. Await the actual blank
        // document and its empty DOM, not merely the new URL; never accept a
        // source-only "clear" acknowledgement as privacy evidence.
        await([&] {
            return source(core.Get()) == cleared_source &&
                evaluate(core.Get(), LR"JS(
                    document.querySelector('#messages') === null &&
                    document.body.textContent.trim() === '' &&
                    document.querySelectorAll('script[src],img').length === 0
                )JS") == L"true";
        }, "retired private chat is absent from the actual new blank WebView2 DOM");
        expect(!surface.ready() && !surface.publish(kMessage) && !surface.jump_to_latest(),
            "stopped chat cannot reuse the retired source or local keyboard navigation");

        // A same-URL reload is outside this receiver's one NavigationId.
        // It discards the receiver even though the host STILL owns that exact
        // private URL. Stop must replace its potentially visible old chat,
        // never skip cleanup merely because the receiver is invalidated.
        expect(surface.open(host), "create live private document before unexpected reload");
        await([&] { return surface.ready(); }, "reloaded-test document handshake");
        expect(surface.publish(kMessage), "real private message before unexpected reload");
        await([&] { return surface.rendered_frames() == 1U &&
                            surface.rendered_messages() == 1U; },
            "private text acknowledged before unexpected own document reload");
        const std::wstring invalidated_url = chatview::WebViewHostTestAccess::local_document(host);
        expect(SUCCEEDED(core->Reload()), "inject a real second navigation of the same own URL");
        await([&] { return surface.discarded(); },
            "second own NavigationStarting retires first nonce and its render authority");
        expect(!surface.ready() && !surface.publish(kMessage),
            "a discarded receiver cannot post new private chat after unexpected reload");
        surface.close();
        const std::wstring invalidated_blank = chatview::WebViewHostTestAccess::local_document(host);
        expect(!invalidated_blank.empty() && invalidated_blank != invalidated_url,
            "Stop blanks an invalidated but still selected private document");
        await([&] {
            return source(core.Get()) == invalidated_blank &&
                evaluate(core.Get(), LR"JS(
                    document.querySelector('#messages') === null &&
                    document.body.textContent.trim() === '' &&
                    document.querySelectorAll('script[src],img').length === 0
                )JS") == L"true";
        }, "invalidated owner has no old private rows in actual blank WebView2 DOM");
        host.close(); expect(!surface.ready(), "host destruction invalidates surface"); surface.close(); core.Reset();
        std::cout << "Native own-chat DOM, memory navigation isolation, setup and teardown passed\n";
    } catch (const std::exception &error) {
        std::cerr << "Native chat test failed: " << error.what() << '\n'; code = 1;
    }
    if (window) DestroyWindow(window);
    CoUninitialize();
    return code;
}
