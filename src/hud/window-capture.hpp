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
    // Conservative content time in the GetTickCount64 domain; zero is invalid.
    // It must not be advanced to GPU-call completion time.
    std::uint64_t content_at_ms = 0;
    // Last observed geometry/counters, not a transaction or persisted log.
    // width/height above remain the last successfully presented content size.
    unsigned received_width = 0, received_height = 0;
    unsigned surface_width = 0, surface_height = 0;
    std::uint64_t recreates = 0, clipped_frames = 0;
    std::int32_t failure_hresult = 0;
    // Requested pool capacity, independent of content and late surface size.
    unsigned pool_width = 0, pool_height = 0;
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
