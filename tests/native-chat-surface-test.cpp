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
        await([&] { return surface.ready(); }, "own document handshake");
        ComPtr<ICoreWebView2> core = chatview::NativeChatSurfaceTestAccess::core(surface);
        const std::wstring first_url = source(core.Get());
        expect(first_url.starts_with(L"https://chatview.invalid/"), "host-owned memory document");
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
        host.show_setup_page();
        expect(!surface.ready() && !surface.publish(kMessage), "setup immediately invalidates private delivery");
        await([&] { return evaluate(core.Get(), L"document.querySelector('h1')?.textContent === 'ChatView needs a chat URL'") == L"true"; }, "setup page actually loads");
        surface.close(); surface.close();
        expect(surface.open(host), "new own document after close");
        await([&] { return surface.ready(); }, "fresh document handshake");
        expect(source(core.Get()) != first_url, "reopen gets distinct document identity");
        expect_cancelled(core.Get(), first_url);
        expect(surface.ready(), "stale document cannot replace current one");
        expect(surface.publish(kMessage), "reopened render");
        await([&] { return surface.rendered_frames() == 1U; }, "fresh counters");
        surface.close();
        // Clear and open can be queued together; an old cancelled event must
        // neither authorize its stale document nor invalidate the newer one.
        expect(surface.open(host), "immediate reopen while clear is queued");
        await([&] { return surface.ready(); }, "queued clear followed by fresh handshake");
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
