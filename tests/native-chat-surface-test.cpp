// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the production DirectComposition/WebView2 host with synthetic chat only.
#include "hud/native-chat-surface.hpp"
#include "hud/webview-host.hpp"
#include <Windows.h>
#include <wrl/event.h>
#include <cwchar>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

// Narrow test-only inspection: do not add a raw browser accessor to the product API.
namespace chatview {
struct NativeChatSurfaceTestAccess {
    static ICoreWebView2 *core(NativeChatSurface &surface) { return surface.webview_.Get(); }
};
}

namespace {
void trace_document(chatview::NativeChatSurface &surface)
{
    using Microsoft::WRL::Callback;
    auto *core = chatview::NativeChatSurfaceTestAccess::core(surface);
    EventRegistrationToken token{};
    core->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
            LPWSTR uri = nullptr; UINT64 id = 0U; BOOL cancelled = FALSE;
            const auto uri_result = args->get_Uri(&uri);
            args->get_NavigationId(&id); args->get_Cancel(&cancelled);
            // This harness loads only a fixed synthetic document.
            std::wcout << L"Own navigation URI prefix: hr=" << uri_result << L" ["
                       << (uri ? std::wstring(uri).substr(0U, 96U) : L"null") << L"]\n";
            std::cout << "Own document navigation: blank=" << (uri && wcscmp(uri, L"about:blank") == 0)
                      << " id=" << id << " cancelled=" << cancelled << '\n';
            CoTaskMemFree(uri); return S_OK;
        }).Get(), &token);
    core->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
            LPWSTR uri = nullptr; LPWSTR value = nullptr;
            args->get_Source(&uri); const auto result = args->TryGetWebMessageAsString(&value);
            std::cout << "Own document message: blank=" << (uri && wcscmp(uri, L"about:blank") == 0)
                      << " string=" << SUCCEEDED(result)
                      << " ready=" << (value && wcsncmp(value, L"chat-ready:", 11U) == 0) << '\n';
            CoTaskMemFree(uri); CoTaskMemFree(value); return S_OK;
        }).Get(), &token);
    core->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [](ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
            BOOL success = FALSE; UINT64 id = 0U; args->get_IsSuccess(&success); args->get_NavigationId(&id);
            std::cout << "Own document complete: success=" << success << " id=" << id << '\n';
            // Fixed expression on this synthetic, initially empty document only.
            sender->ExecuteScript(L"JSON.stringify({location:location.href,ready:document.readyState,bridge:typeof window.chrome.webview.postMessage,status:document.getElementById('status')?.textContent,scripts:document.scripts.length})",
                Callback<ICoreWebView2ExecuteScriptCompletedHandler>([](HRESULT result, LPCWSTR json) -> HRESULT {
                    std::wcout << L"Own document probe: hr=" << result << L" " << (json ? json : L"null") << L'\n';
                    return S_OK;
                }).Get());
            return S_OK;
        }).Get(), &token);
}
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
constexpr wchar_t kMessage[] = LR"JSON({"type":"chat-snapshot","version":1,"snapshot":{"state":"subscribed","received":1,"messages":[{"nickname":"모의 사용자 <script>","content":"한글 😀 <img onerror=evil()>","messageTime":1700000000000}]}})JSON";
}
int main()
{
    // Keep this test isolated from real streamer settings and WebView profiles.
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
        trace_document(surface);
        await([&] { return surface.ready(); }, "own document handshake");
        expect(surface.publish(kMessage), "publish synthetic Unicode message");
        await([&] { return surface.rendered_frames() == 1U; }, "actual DOM render acknowledgement");
        expect(surface.rendered_messages() == 1U, "one rendered text row");
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
        await([&] { return !surface.ready(); }, "another document invalidates chat authority");
        expect(!surface.publish(kMessage), "setup page cannot receive chat");
        surface.close(); surface.close();
        expect(surface.open(host), "new own document after close");
        await([&] { return surface.ready(); }, "fresh document handshake");
        expect(surface.publish(kMessage), "reopened render");
        await([&] { return surface.rendered_frames() == 1U; }, "fresh counters");
        // The external navigation immediately changes host ownership; no need
        // to reach a real service to prove a private payload will be refused.
        expect(host.navigate(L"https://www.youtube.com/live_chat?is_popout=1&v=abcdefghijk"), "external navigation accepted by existing policy");
        expect(!surface.publish(kMessage), "external page cannot receive chat");
        surface.close();
        host.close();
        expect(!surface.publish(kMessage), "closed host cannot receive chat");
        expect(host.initialize(window), "host can be recreated");
        await([&] { return host.ready(); }, "recreated host ready");
        expect(surface.open(host), "surface attaches to recreated host");
        await([&] { return surface.ready(); }, "recreated surface ready");
        // Destroy host first to exercise weak callback ownership.
        host.close(); expect(!surface.ready(), "host destruction invalidates surface"); surface.close();
        std::cout << "Native own-chat render, schema rejection, navigation and teardown passed\n";
    } catch (const std::exception &error) {
        std::cerr << "Native chat test failed: " << error.what() << '\n'; code = 1;
    }
    if (window) DestroyWindow(window);
    CoUninitialize();
    return code;
}
