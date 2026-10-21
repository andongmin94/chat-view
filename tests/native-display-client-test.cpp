// SPDX-License-Identifier: GPL-2.0-or-later
// Launched by the Node fixture; synthetic keys arrive only over inherited stdin.
#include "hud/display-client.hpp"
#include "hud/hud-window.hpp"
#include "hud/native-chat-connection.hpp"
#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"
#include <Windows.h>
#include <wrl/event.h>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace chatview {
struct NativeChatConnectionTestAccess {
    static DisplayClient &client(NativeChatConnection &c) { return c.client_; }
    static const std::wstring &login_url(NativeChatConnection &c) { return c.login_url_; }
    static HWND dialog(NativeChatConnection &c) { return c.dialog_; }
    static HWND host(NativeChatConnection &c) { return c.host_; }
    static NativeChatSurface &surface(NativeChatConnection &c) { return c.surface_; }
    static bool active(NativeChatConnection &c) { return c.active_; }
    static bool cancel_waiting(NativeChatConnection &c) { return c.login_cancel_waiting_; }
    static bool force_new_login(NativeChatConnection &c) { return c.force_new_login_; }
    static const std::wstring &previous_code(NativeChatConnection &c) { return c.previous_login_code_; }
    static const std::optional<DisplayConnectionState> &connection(NativeChatConnection &c) { return c.connection_state_; }
};
struct NativeChatSurfaceTestAccess {
    static ICoreWebView2 *core(NativeChatSurface &s) { return s.webview_.Get(); }
};
}
namespace {
void expect(bool value, const char *label) { if (!value) throw std::runtime_error(label); }
std::wstring control_text(HWND dialog, int id)
{
    wchar_t value[1024]{};
    GetDlgItemTextW(dialog, id, value, 1024);
    return value;
}
void expect_video_scope(HWND dialog)
{
    const HWND scope = GetDlgItem(dialog, 111);
    const auto text = control_text(dialog, 111);
    expect(scope && IsWindowVisible(scope) &&
        text == L"수신 영상의 HUD 제외: 미검증\n채팅 연결·OBS 활성 보고는 영상 검증이 아닙니다.",
        "video warning is independent of successful chat/role delivery");
    RECT panel_client{}, scope_bounds{};
    expect(GetClientRect(dialog, &panel_client) && GetWindowRect(scope, &scope_bounds),
        "video warning has bounded real panel geometry");
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT *>(&scope_bounds), 2);
    expect(scope_bounds.left >= 0 && scope_bounds.top >= 0 &&
        scope_bounds.right <= panel_client.right && scope_bounds.bottom <= panel_client.bottom,
        "video warning fits both the streaming and gaming panel client area");
    RECT client{};
    expect(GetClientRect(scope, &client) != FALSE, "video scope control bounds");
    const HDC dc = GetDC(scope);
    expect(dc != nullptr, "video scope text measurement DC");
    const auto font = reinterpret_cast<HFONT>(SendMessageW(scope, WM_GETFONT, 0, 0));
    const auto old = font ? SelectObject(dc, font) : nullptr;
    RECT required{0, 0, client.right, 0};
    const int height = DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &required,
        DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    if (old && old != HGDI_ERROR) SelectObject(dc, old);
    ReleaseDC(scope, dc);
    expect(height > 0 && required.bottom <= client.bottom && required.right <= client.right,
        "persistent two-line video warning fits its actual native font and control");
}
void pump(chatview::NativeChatConnection *connection = nullptr)
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        expect(message.message != WM_QUIT, "HUD terminated unexpectedly");
        if (connection && connection->dispatch(message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    if (connection) connection->tick();
}
void await(const std::function<bool()> &predicate, const char *label,
           chatview::NativeChatConnection *connection = nullptr, ULONGLONG timeout = 15000U)
{
    const ULONGLONG deadline = GetTickCount64() + timeout;
    while (!predicate()) {
        expect(GetTickCount64() < deadline, label); pump(connection);
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
std::wstring source_url(ICoreWebView2 *core)
{
    LPWSTR value = nullptr;
    expect(SUCCEEDED(core->get_Source(&value)) && value, "read current owned WebView2 source");
    std::wstring result(value);
    CoTaskMemFree(value);
    return result;
}
std::wstring evaluate(ICoreWebView2 *core, const wchar_t *script, chatview::NativeChatConnection &connection)
{
    struct Result { bool done = false; HRESULT code = E_FAIL; std::wstring value; };
    const auto result = std::make_shared<Result>();
    expect(SUCCEEDED(core->ExecuteScript(script,
        Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [result](HRESULT code, LPCWSTR value) -> HRESULT {
                result->code = code; if (value) result->value = value; result->done = true; return S_OK;
            }).Get())), "queue DOM assertion");
    await([&] { return result->done; }, "DOM assertion completion", &connection);
    expect(SUCCEEDED(result->code), "DOM assertion succeeded"); return result->value;
}
void validate_inputs(const std::wstring &origin, const std::wstring &ticket)
{
    chatview::DisplayClient client;
    for (const auto &bad : {L"http://localhost:47831", L"http://example.com", L"https://user:pass@example.com",
             L"https://example.com/path", L"https://example.com?key=x", L"https://example.com/#x",
             L"file:///test", L"https://example.com\\path", L"https://example.com/%2e", L" https://example.com"}) {
        expect(!client.start(bad, ticket, true), "reject unsafe origin without connecting");
    }
    expect(!client.start(origin, ticket), "plain loopback requires explicit opt-in");
    expect(!client.start(origin, L"not-a-ticket", true), "bad ticket rejected before I/O");
}
struct BrowserProbe {
    std::wstring origin, last_url;
    unsigned calls = 0U;
    bool fail_next = false, reenter_next = false;
} browser_probe;
void gateway_ui(const std::wstring &origin, const std::string &mode)
{
    wchar_t temporary[MAX_PATH]{}; expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary directory");
    // This test runs multiple child processes. A PID-derived profile folder
    // can collide with the previous run's retained WebView2/DPAPI data and
    // fail before any actual native chat assertion executes.
    wchar_t unique[MAX_PATH]{};
    expect(GetTempFileNameW(temporary, L"CVD", 0, unique) != 0, "reserve unique WebView2 profile");
    expect(DeleteFileW(unique) != FALSE && CreateDirectoryW(unique, nullptr) != FALSE,
        "isolated WebView2 profile");
    const std::wstring profile(unique);
    expect(SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str()) != FALSE,
        "bind private WebView2/DPAPI test profile");
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "create production HUD");
    const bool gaming = mode == "gateway-gaming" || mode == "gateway-provider-loss-gaming" ||
        mode == "gateway-transport-burst-gaming";
    const auto role = gaming ? chatview::DisplayRole::Gaming : chatview::DisplayRole::Streaming;
    browser_probe.origin = origin;
    chatview::NativeChatConnection connection(hud, role, [](HWND window, const wchar_t *url) {
        const std::wstring value(url), prefix = browser_probe.origin + L"/login/";
        expect(value.starts_with(prefix) && value.size() == prefix.size() + 32U &&
            std::all_of(value.begin() + static_cast<std::ptrdiff_t>(prefix.size()), value.end(), [](wchar_t c) {
                return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
            }), "browser receives only the original origin and canonical non-secret request path");
        browser_probe.last_url = value; ++browser_probe.calls;
        if (std::exchange(browser_probe.reenter_next, false)) {
            const auto calls = browser_probe.calls;
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(117, BN_CLICKED), 0);
            expect(browser_probe.calls == calls, "shell reentrancy cannot open another browser");
        }
        std::cout << "browser-opened\n" << std::flush;
        return !std::exchange(browser_probe.fail_next, false);
    });
    hud.show_ready();
    await([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "HUD startup", &connection);
    const HWND host = chatview::NativeChatConnectionTestAccess::host(connection);
    const UINT open_message = RegisterWindowMessageW(chatview::kOpenNativeChatMessageName);
    const UINT query_message = RegisterWindowMessageW(chatview::kQueryNativeChatMessageName);
    expect(host && open_message && query_message, "native control endpoints attached to the real HUD");
    const auto query = [&] {
        DWORD_PTR result = 0;
        expect(SendMessageTimeoutW(host, query_message, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
            40, &result) != 0, "bounded read-only native query");
        return chatview::decode_native_chat_status(static_cast<std::uint64_t>(result));
    };
    const auto open = [&](WPARAM argument = 0) {
        DWORD_PTR result = 0;
        expect(SendMessageTimeoutW(host, open_message, argument, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
            1500, &result) != 0, "bounded native panel command");
        return result;
    };
    expect(query() == chatview::NativeChatStatus::Idle, "read-only query does not create a login");
    expect(open(1) == 0 && !chatview::NativeChatConnectionTestAccess::dialog(connection),
        "native control rejects unexpected payload before opening a panel");
    expect(open() == 1, "registered command opens the existing native panel");
    HWND dialog = chatview::NativeChatConnectionTestAccess::dialog(connection);
    expect(dialog && open() == 1 && dialog == chatview::NativeChatConnectionTestAccess::dialog(connection),
        "repeat open reuses one panel without a duplicate connection");
    expect(query() == chatview::NativeChatStatus::Idle && !chatview::NativeChatConnectionTestAccess::active(connection),
        "open acknowledgement is not approval, transport or navigation");
    // A local test changes only this panel's affinity; an open request must not
    // silently repair/ignore lost protection or reveal that window.
    ShowWindow(dialog, SW_HIDE);
    expect(SetWindowDisplayAffinity(dialog, WDA_NONE) != FALSE, "inject lost panel protection");
    expect(open() == 0 && !IsWindowVisible(dialog), "open refused when existing panel protection is lost");
    expect(SetWindowDisplayAffinity(dialog, WDA_EXCLUDEFROMCAPTURE) != FALSE, "restore test panel protection");
    expect(open() == 1 && IsWindowVisible(dialog), "open succeeds only after protection is restored");
    expect_video_scope(dialog);
    expect(control_text(dialog, 109).find(gaming ? L"게임 PC" : L"송출 PC") != std::wstring::npos, "panel identifies launch role before consent");
    expect(control_text(dialog, 110).find(L"확인되지 않음") != std::wstring::npos, "no session claim before authorization");
    SetDlgItemTextW(dialog, 101, origin.c_str());
    SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(104, BN_CLICKED), 0);
    expect(GetDlgItem(dialog, 102) == nullptr && GetDlgItem(dialog, 107) != nullptr, "login panel has no manual key input");
    auto &client = chatview::NativeChatConnectionTestAccess::client(connection);
    auto &surface = chatview::NativeChatConnectionTestAccess::surface(connection);
    const auto reopen = [&] { SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(117, BN_CLICKED), 0); };
    await([&] { return browser_probe.calls == 1U && IsWindowEnabled(GetDlgItem(dialog, 117)); },
        "initial URL is delivered once and the same request can be reopened", &connection);
    const auto first_url = browser_probe.last_url;
    const auto first_notice = control_text(dialog, 106);
    auto code = first_url.substr(first_url.size() - 6U);
    for (auto &c : code) if (c >= L'a' && c <= L'f') c = static_cast<wchar_t>(c - (L'a' - L'A'));
    expect(control_text(dialog, 118) == code, "pending confirmation is persistently visible beside reopen");
    expect(client.login_pending() && !client.can_resume_current() &&
        query() == chatview::NativeChatStatus::AwaitingApproval &&
        chatview::NativeChatSurfaceTestAccess::core(surface) != nullptr &&
        surface.rendered_frames() == 0U && surface.rendered_messages() == 0U,
        "a prewarmed own document has no authorized chat or first-text ACK before browser consent");
    expect(control_text(dialog, 117) == L"브라우저 승인 페이지 다시 열기", "explicit reopen action is labelled");
    RECT button_bounds{}, previous_bounds{}, display_bounds{}, dialog_bounds{};
    expect(GetWindowRect(GetDlgItem(dialog, 117), &button_bounds) &&
        GetWindowRect(GetDlgItem(dialog, 119), &previous_bounds) &&
        GetWindowRect(GetDlgItem(dialog, 113), &display_bounds) && GetWindowRect(dialog, &dialog_bounds),
        "actual native reopen, old-request warning and status bounds");
    expect(button_bounds.bottom <= previous_bounds.top && previous_bounds.bottom <= display_bounds.top &&
        previous_bounds.right <= dialog_bounds.right && button_bounds.right <= dialog_bounds.right,
        "current and previous request rows do not overlap or overflow the protected panel");
    expect(control_text(dialog, 119).empty(), "there is no obsolete request before a cancellation");
    expect((GetDlgItem(dialog, 116) != nullptr) == gaming, "reopen does not introduce gaming video controls in OBS role");
    std::wstring unused;
    expect(!client.take_login_url(unused), "tick cannot automatically reopen an already delivered URL");

    ShowWindow(dialog, SW_HIDE); reopen();
    expect(browser_probe.calls == 1U, "hidden panel rejects a queued reopen command");
    expect(open() == 1 && browser_probe.calls == 1U, "showing the panel is not browser navigation");
    expect(SetWindowDisplayAffinity(dialog, WDA_NONE) != FALSE, "inject lost protection during pending login");
    reopen(); expect(browser_probe.calls == 1U && !IsWindowVisible(dialog), "unprotected panel cannot reopen a browser");
    expect(SetWindowDisplayAffinity(dialog, WDA_EXCLUDEFROMCAPTURE) != FALSE && open() == 1,
        "restore panel protection without changing the pending request");
    SetDlgItemTextW(dialog, 101, L"https://other.invalid/edited?credential=not-a-real-key");
    SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageW(dialog, 107, BM_SETCHECK, BST_CHECKED, 0);
    browser_probe.reenter_next = true; reopen();
    expect(browser_probe.calls == 2U && browser_probe.last_url == first_url, "reopen ignores edited origin and login choices");
    browser_probe.fail_next = true; reopen();
    expect(browser_probe.calls == 3U && client.login_pending() &&
        chatview::NativeChatConnectionTestAccess::active(connection), "reopen launch failure preserves the current request");
    expect(control_text(dialog, 106).find(L"그대로 대기") != std::wstring::npos, "reopen failure explains preserved request");
    reopen(); expect(browser_probe.calls == 4U && browser_probe.last_url == first_url,
        "explicit retry uses exactly the same request without resetting its deadline");
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(104, BN_CLICKED), 0);
    expect(browser_probe.calls == 4U && client.login_pending(), "connect while pending never starts a duplicate login");
    expect(first_notice.find(L"확인 번호") != std::wstring::npos && control_text(dialog, 118) == code,
        "original confirmation survives reopen, failure and edited controls");

    if (mode.starts_with("login-")) {
        if (mode == "login-cancel" || mode == "login-cancel-unconfirmed") {
            const auto before = GetTickCount64();
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(105, BN_CLICKED), 0);
            expect(GetTickCount64() - before < 200U, "stop never waits for server cancel I/O");
            expect(!client.login_pending() && !client.can_resume_current() &&
                chatview::NativeChatConnectionTestAccess::cancel_waiting(connection) &&
                chatview::NativeChatConnectionTestAccess::force_new_login(connection),
                "stop fences old consent and prevents saved-account substitution without server proof");
            expect(control_text(dialog, 106).find(L"확인 중") != std::wstring::npos &&
                control_text(dialog, 118).empty(), "pending cancellation clears confirmation code immediately");
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(104, BN_CLICKED), 0);
            expect(browser_probe.calls == 4U, "new login cannot start during asynchronous cancellation");
        } else {
            if (mode == "login-denied") std::cout << "deny-login\n" << std::flush;
            // No connection.tick(): test the client's request fence even when
            // its final status has not yet reached the UI. Expiry uses a shorter
            // fixture response lifetime, never a larger production time budget.
            await([&] { return !client.login_pending(); }, "denied or expired request loses reopening eligibility", nullptr, 5000U);
        }
        reopen(); expect(browser_probe.calls == 4U, "terminal request cannot be reopened before the next UI tick");
        await([&] { return !client.running() && !chatview::NativeChatConnectionTestAccess::active(connection); },
            "old login ends without an automatic replacement", &connection,
            mode == "login-cancel-unconfirmed" ? 5000U : 2000U);
        if (mode == "login-cancel" || mode == "login-cancel-unconfirmed") {
            const auto expected = mode == "login-cancel" ? L"서버 취소를 확인했습니다" : L"서버 취소 미확인";
            await([&] { return control_text(dialog, 106).find(expected) != std::wstring::npos; },
                "native panel reports confirmed or unconfirmed cancellation after worker retirement", &connection);
            expect(!chatview::NativeChatConnectionTestAccess::cancel_waiting(connection),
                "terminal cancel result is consumed only once");
            if (mode == "login-cancel-unconfirmed") {
                expect(chatview::NativeChatConnectionTestAccess::previous_code(connection) == code &&
                    control_text(dialog, 119).find(code) != std::wstring::npos,
                    "unconfirmed old request has an independent persistent code, not the current login code");
                expect(control_text(dialog, 106).find(L"미확인") != std::wstring::npos,
                    "ambiguous response never claims the old request was revoked");
            } else {
                expect(chatview::NativeChatConnectionTestAccess::previous_code(connection).empty() &&
                    control_text(dialog, 119).empty(), "acknowledged remote cancellation clears the old warning");
            }
        }
        expect(!IsWindowEnabled(GetDlgItem(dialog, 117)) &&
            chatview::NativeChatConnectionTestAccess::login_url(connection).empty() && control_text(dialog, 118).empty() &&
            !client.can_resume_current(),
            "ended login keeps neither stale URL nor approval");
        SetDlgItemTextW(dialog, 101, origin.c_str());
        SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_CHECKED, 0);
        // A remembered unrelated credential must never silently replace the
        // explicit fresh-browser flow after Stop, even if this box is checked.
        const bool cancelled = mode == "login-cancel" || mode == "login-cancel-unconfirmed";
        SendDlgItemMessageW(dialog, 107, BM_SETCHECK, cancelled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(104, BN_CLICKED), 0);
        await([&] { return browser_probe.calls == 5U && client.login_pending(); }, "explicit new login after termination", &connection);
        expect(browser_probe.last_url != first_url, "new login has a new request, not revived old consent");
        if (cancelled) {
            expect(chatview::NativeChatConnectionTestAccess::force_new_login(connection),
                "fresh request retains no-saved-account fence until real receiving");
            if (mode == "login-cancel-unconfirmed") {
                expect(control_text(dialog, 118) != code &&
                    control_text(dialog, 119).find(code) != std::wstring::npos &&
                    control_text(dialog, 106).find(L"이전 취소 미확인") != std::wstring::npos,
                    "native panel distinguishes new code from old unconfirmed browser consent");
            }
        }
    }
    const auto calls = browser_probe.calls;
    std::cout << "approve-login\n" << std::flush;
    await([&] { return !client.login_pending(); }, "collected approval retires the login URL before DOM readiness", nullptr, 5000U);
    reopen(); expect(browser_probe.calls == calls, "queued reopen after collection cannot reopen an old consent page");
    await([&] { return surface.rendered_messages() == 1U; }, "WinHTTP gateway frame rendered", &connection);
    await([&] { return query() == chatview::NativeChatStatus::Receiving; },
        "query reports receiving only after native rendering acknowledgement", &connection);
    const auto &state = chatview::NativeChatConnectionTestAccess::connection(connection);
    expect(state && state->membership.role == role &&
        state->gaming_connections == (gaming ? 1U : 0U) && state->streaming_connections == (gaming ? 0U : 1U),
        "native controls receive authorized role and same-session display counts");
    expect(control_text(dialog, 110) == chatview::connection_summary(*state), "panel displays actual accepted session metadata");
    expect(control_text(dialog, 110).find(L"영상 제외 미검증") != std::wstring::npos, "role connection is not capture qualification");
    expect_video_scope(dialog);
    Microsoft::WRL::ComPtr<ICoreWebView2> core = chatview::NativeChatSurfaceTestAccess::core(surface);
    expect(evaluate(core.Get(), LR"JS(
        document.querySelector('#messages li b').textContent === '검증 사용자' &&
        document.querySelector('#messages li .content').textContent === '안녕 😀 <img onerror=evil()>' &&
        document.querySelectorAll('#messages img,#messages script').length === 0
    )JS", connection) == L"true", "real DOM Unicode/inert markup");
    expect(!IsWindowEnabled(GetDlgItem(dialog, 117)) &&
        chatview::NativeChatConnectionTestAccess::login_url(connection).empty() && control_text(dialog, 118).empty(),
        "receiving cannot retain a login reopen action or confirmation code");
    expect(!chatview::NativeChatConnectionTestAccess::force_new_login(connection) &&
        control_text(dialog, 119).empty(),
        "only the fresh confirmed approval retires the saved-account fence and transient old-code warning");
    const auto approved_membership = state->membership;
    // A completed older WebView2 navigation can remain queued after the
    // private receiver is already displaying the new subscription. The HUD
    // must ignore both stale failure/retry and stale success/loading messages.
    const UINT health_query = RegisterWindowMessageW(chatview::kQueryHudHealthMessageName);
    expect(health_query != 0U, "registered existing read-only HUD health query");
    const LRESULT health_before = SendMessageW(host, health_query, 0U, 0L);
    SendMessageW(host, chatview::kWebViewNavigationFailedMessage,
        COREWEBVIEW2_WEB_ERROR_STATUS_UNEXPECTED_ERROR, 0L);
    SendMessageW(host, chatview::kWebViewDocumentReadyMessage, 0U, 0L);
    expect(SendMessageW(host, health_query, 0U, 0L) == health_before &&
        surface.ready() && surface.rendered_messages() == 1U,
        "stale host navigation signals cannot turn live private chat into retry/loading");
    expect(open() == 1 && dialog == chatview::NativeChatConnectionTestAccess::dialog(connection) &&
        chatview::NativeChatConnectionTestAccess::connection(connection)->membership == approved_membership,
        "opening an approved connection does not replace its membership");
    if (mode.starts_with("gateway-transport-burst")) {
        const std::wstring previous_document = source_url(core.Get());
        const auto previous_frames = surface.rendered_frames();
        expect(previous_frames != 0U && surface.rendered_messages() == 1U,
            "original approved document displays its first subscribed chat before transport loss");
        // The worker owns socket I/O. Intentionally DO NOT tick the native
        // owner while the server closes that socket and validates a renewal.
        // If Reconnecting is overwritten by Receiving, the old private DOM
        // will incorrectly survive instead of being replaced.
        std::cout << "transport-burst\n" << std::flush;
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        const ULONGLONG due = GetTickCount64() + 6000U;
        for (;;) {
            DWORD available = 0U;
            expect(PeekNamedPipe(input, nullptr, 0U, nullptr, &available, nullptr) != FALSE,
                "native test reply pipe remains valid");
            if (available) {
                std::string reply;
                std::getline(std::cin, reply);
                expect(reply == "transport-resumed", "fixture renewed the same approved transport");
                break;
            }
            expect(GetTickCount64() < due, "bounded transport renewal without native UI polling");
            Sleep(10);
        }
        await([&] {
            return source_url(core.Get()) != previous_document &&
                surface.ready() && surface.rendered_messages() == 1U &&
                query() == chatview::NativeChatStatus::Receiving;
        }, "reconnected transport has a newly owned document and fresh render ACK", &connection);
        expect(evaluate(core.Get(), LR"JS(
            document.querySelectorAll('#messages li').length === 1 &&
            document.querySelector('#messages li b').textContent === '복구 사용자' &&
            document.querySelector('#messages li .content').textContent === '새 소켓의 첫 채팅 😀' &&
            !document.getElementById('messages').textContent.includes('안녕 😀')
        )JS", connection) == L"true",
            "old private chat is gone and only the new subscribed session text remains");
        expect(chatview::NativeChatConnectionTestAccess::connection(connection)->membership == approved_membership &&
            !IsWindowEnabled(GetDlgItem(dialog, 112)),
            "quick transport recovery preserves membership and needs no manual reapproval");
    }
    if (mode == "gateway-provider-burst") {
        // Delay only the UI's consumption, not the WinHTTP receiver: both
        // authenticated frames must reach the worker before the owner polls.
        // A transient disconnect may not be overwritten by the new chat.
        const auto before = surface.rendered_frames();
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            globalThis.providerBurstCleared = false;
            new MutationObserver(() => {
                if (list.querySelectorAll('li').length === 0)
                    globalThis.providerBurstCleared = true;
            }).observe(list, {childList: true});
            return list.querySelectorAll('li').length === 1;
        })())JS", connection) == L"true", "observe the owned chat DOM before fast provider recovery");
        std::cout << "provider-burst\n" << std::flush;
        const ULONGLONG delayed_until = GetTickCount64() + 350U;
        while (GetTickCount64() < delayed_until) {
            pump(nullptr);
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        await([&] {
            return surface.rendered_frames() >= before + 2U && surface.rendered_messages() == 1U;
        }, "fast provider loss and recovery render two distinct native frames", &connection);
        expect(evaluate(core.Get(), LR"JS((() => {
            const list = document.getElementById('messages');
            return globalThis.providerBurstCleared === true &&
                list.querySelectorAll('li').length === 1 &&
                list.querySelector('li b').textContent === '복구 사용자' &&
                list.querySelector('li .content').textContent === '새 구독의 첫 채팅 😀';
        })())JS", connection) == L"true",
            "actual WebView2 first clears stale chat, then displays the new subscription");
        expect(query() == chatview::NativeChatStatus::Receiving &&
            !IsWindowEnabled(GetDlgItem(dialog, 112)) &&
            chatview::NativeChatConnectionTestAccess::connection(connection)->membership == approved_membership,
            "rapid recovery preserves the original approval and exact fresh first-text acknowledgement");
    }
    if (mode.starts_with("gateway-provider-loss")) {
        expect(!IsWindowEnabled(GetDlgItem(dialog, 112)),
               "a live first-text ACK does not offer redundant manual reconnect");
        std::cout << "provider-loss\n" << std::flush;
        await([&] {
            return surface.rendered_messages() == 0U &&
                   query() == chatview::NativeChatStatus::Connecting &&
                   IsWindowEnabled(GetDlgItem(dialog, 112)) != FALSE;
        }, "provider disconnect clears the native first-text claim and enables explicit recovery", &connection);
        expect(control_text(dialog, 106).find(L"치지직 채팅 구독이 끊겼습니다") != std::wstring::npos &&
               control_text(dialog, 112) == L"현재 승인으로 채팅 표시 다시 시도",
               "interrupted subscription explains the loss and permits same-approval retry");
        expect(evaluate(core.Get(), L"document.querySelectorAll('#messages li').length === 0", connection) == L"true",
               "old provider chat cannot remain visible during automatic resubscription");
        expect(chatview::NativeChatConnectionTestAccess::connection(connection)->membership == approved_membership,
               "transport interruption does not grant or replace a role");
        std::cout << "provider-return\n" << std::flush;
        await([&] {
            return surface.rendered_messages() == 1U &&
                   query() == chatview::NativeChatStatus::Receiving &&
                   !IsWindowEnabled(GetDlgItem(dialog, 112));
        }, "fresh provider first chat restores the original native HUD and disables redundant retry", &connection);
        expect(evaluate(core.Get(), LR"JS(
            document.querySelectorAll('#messages li').length === 1 &&
            document.querySelector('#messages li b').textContent === '복구 사용자' &&
            document.querySelector('#messages li .content').textContent === '새 구독의 첫 채팅 😀'
        )JS", connection) == L"true", "fresh chat replaces the old provider session text on the owned DOM");
        expect(chatview::NativeChatConnectionTestAccess::connection(connection)->membership == approved_membership,
               "the resumed subscription preserves the original gaming/streaming approval");
    }
    std::cout << "rendered\n" << std::flush;
    await([&] { return !chatview::NativeChatConnectionTestAccess::active(connection); }, "grant revoke ends native delivery", &connection);
    expect(!chatview::NativeChatConnectionTestAccess::connection(connection), "revocation clears native session state");
    await([&] {
        const auto phase = query();
        expect(phase == chatview::NativeChatStatus::Stopping || phase == chatview::NativeChatStatus::Idle,
            "revocation immediately removes receiving and return eligibility");
        return phase == chatview::NativeChatStatus::Idle;
    }, "revoked worker finishes without a retained return approval", &connection, 2000U);
    expect(control_text(dialog, 110).find(L"확인되지 않음") != std::wstring::npos, "revocation clears session status text");
    expect_video_scope(dialog);
    await([&] { return evaluate(core.Get(), L"document.querySelectorAll('#messages li').length === 0", connection) == L"true"; },
          "revocation clears actual DOM", &connection);
    connection.close();
    expect(query() == chatview::NativeChatStatus::Unavailable && open() == 0,
        "closed connection removes its subclass instead of leaving a stale handler");
    hud.destroy();
}
void transport(const std::wstring &origin, const std::wstring &ticket, const std::string &mode)
{
    chatview::DisplayClient client;
    expect(client.start(origin, ticket, true), "start read-only native client");
    if (mode == "cancel") {
        std::string requested; std::getline(std::cin, requested);
        expect(requested == "requested", "fixture has a pending HTTP exchange");
        const auto before = GetTickCount64(); client.stop();
        expect(GetTickCount64() - before < 200U, "stop does not wait for network");
        chatview::DisplayUpdate update;
        expect(client.take(update) && update.envelope.empty() && !update.connection, "stop immediately clears chat and status mailbox");
        await([&] { return !client.running(); }, "cancel pending async HTTP", nullptr, 2000U);
        return;
    }
    bool received = false, terminal = false;
    const auto before = GetTickCount64();
    await([&] {
        chatview::DisplayUpdate update;
        if (client.take(update)) {
            if (update.status == chatview::DisplayStatus::Receiving) {
                received = true;
                expect(update.connection.has_value(), "receiving requires validated connection metadata");
                expect(update.envelope.find(L"broadcastSessionId") == std::wstring::npos &&
                    update.envelope.find(L"connectionId") == std::wstring::npos,
                    "connection metadata does not reach the renderer");
            }
            if (update.status == chatview::DisplayStatus::Ended || update.status == chatview::DisplayStatus::Failed ||
                update.status == chatview::DisplayStatus::RoleMismatch) {
                terminal = update.envelope.empty() && !update.connection;
            }
        }
        return !client.running();
    }, "transport terminates", nullptr, mode == "idle" ? 19000U : 10000U);
    chatview::DisplayUpdate update;
    if (client.take(update)) terminal = update.envelope.empty() && !update.connection;
    expect(terminal, "terminal status retains no old chat or session metadata");
    if (mode == "local-expiry") {
        expect(received, "fixture delivered before expiry");
        expect(GetTickCount64() - before < 4000U, "local lease expires despite continuing server frames");
    } else if (mode != "idle") expect(!received, "invalid transport never publishes a display frame");
}
}
int main(int argc, char **argv)
{
    try {
        expect(argc == 2, "scenario required");
        std::string origin_line, ticket_line;
        std::getline(std::cin, origin_line); std::getline(std::cin, ticket_line);
        expect(origin_line.size() < 2048U && ticket_line.size() == 64U, "bounded fixture input");
        const std::wstring origin(origin_line.begin(), origin_line.end()), ticket(ticket_line.begin(), ticket_line.end());
        validate_inputs(origin, ticket);
        const std::string mode = argv[1];
        if (mode == "gateway" || mode == "gateway-gaming" ||
            mode.starts_with("gateway-provider-loss") || mode == "gateway-provider-burst" ||
            mode.starts_with("gateway-transport-burst") || mode.starts_with("login-")) {
            expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM startup");
            try { gateway_ui(origin, mode); } catch (...) { CoUninitialize(); throw; }
            CoUninitialize();
        } else transport(origin, ticket, argv[1]);
        std::cout << "Native display scenario passed\n"; return 0;
    } catch (const std::exception &error) {
        std::cerr << "Native display test failed: " << error.what() << '\n'; return 1;
    }
}
