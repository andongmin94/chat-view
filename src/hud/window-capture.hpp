// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <thread>

namespace chatview {
enum class WindowCaptureStatus { Stopped, Starting, Waiting, Capturing, SourceLost, Failed };
struct WindowCaptureSnapshot {
    WindowCaptureStatus status = WindowCaptureStatus::Stopped;
    unsigned width = 0, height = 0;
    std::uint64_t frames = 0;
};
// WGC window -> bounded GPU copy -> opaque, letterboxed output HWND. No screen
// capture fallback, encoder, audio, network, provider token or OBS dependency.
// Call on the UI owner thread; the worker exclusively owns its GPU objects.
class WindowCapture final {
public:
    WindowCapture() = default;
    ~WindowCapture();
    WindowCapture(const WindowCapture &) = delete;
    WindowCapture &operator=(const WindowCapture &) = delete;
    [[nodiscard]] bool start(HWND source, HWND output) noexcept;
    void stop() noexcept;
    void close() noexcept; // final HWND-owner shutdown only; joins GPU teardown
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] WindowCaptureSnapshot snapshot() const noexcept;
private:
    struct State;
    static void run(std::shared_ptr<State>, HWND source, HWND output, DWORD source_process, DWORD source_thread) noexcept;
    std::shared_ptr<State> state_;
    std::thread worker_;
    bool closing_ = false; // UI-thread reentrancy fence, not worker state
};
}
