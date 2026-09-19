// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the production DirectComposition/WebView2 host with synthetic chat only.
#include "hud/native-chat-surface.hpp"
#include "hud/webview-host.hpp"
#include <Windows.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
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
