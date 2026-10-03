// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/capture-worker-wait.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>

namespace {
constexpr UINT sent_message = WM_APP + 1, posted_message = WM_APP + 2;
unsigned sent = 0, posted = 0;
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == sent_message) { ++sent; return 73; }
    if (message == posted_message) { ++posted; return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
void expect(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
}
int main()
{
    try {
        WNDCLASSW klass{}; klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpfnWndProc = procedure; klass.lpszClassName = L"ChatView.CaptureWaitTest";
        expect(RegisterClassW(&klass) != 0, "register window");
        const HWND window = CreateWindowExW(0, klass.lpszClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, klass.hInstance, nullptr);
        expect(window != nullptr, "create message owner");
        expect(PostMessageW(window, posted_message, 0, 0) != FALSE, "queue unrelated posted command");
        PostQuitMessage(19);
        std::atomic<bool> replied{false};
        std::thread worker([&] { replied.store(SendMessageW(window, sent_message, 0, 0) == 73); });
        chatview::join_capture_worker(worker);
        expect(replied.load() && sent == 1 && !worker.joinable(), "sent call and join complete without deadlock");
        expect(posted == 0, "shutdown wait does not dispatch queued commands");
        MSG message{};
        expect(PeekMessageW(&message, window, posted_message, posted_message, PM_REMOVE) != FALSE &&
            message.message == posted_message, "posted command is still queued");
        DispatchMessageW(&message);
        expect(posted == 1, "posted command belongs to normal pump only");
        expect(PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE &&
            message.message == WM_QUIT && message.wParam == 19, "WM_QUIT and exit code are retained");
        chatview::join_capture_worker(worker); // already joined is harmless
        DestroyWindow(window);
        std::cout << "Capture shutdown: sent-message progress, posted-command isolation and WM_QUIT preservation passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
