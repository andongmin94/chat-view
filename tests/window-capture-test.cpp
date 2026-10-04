// SPDX-License-Identifier: GPL-2.0-or-later
// Real WGC/GPU/window pixels, but synthetic source/HUD and one desktop. This
// tests window isolation, NOT a physical capture card, audio or game support.
#include "hud/window-capture.hpp"
#include "hud/video-frame-time.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr wchar_t source_class[] = L"ChatView.SyntheticVideoSource";
constexpr COLORREF video_color = RGB(20, 100, 180), hud_color = RGB(240, 20, 180);
constexpr COLORREF portrait_color = RGB(180, 90, 20), wide_color = RGB(20, 180, 90);
constexpr UINT static_resize = WM_APP + 41, animate_source = WM_APP + 42, sequence_resize = WM_APP + 43;
COLORREF sequence_color = CLR_INVALID; // only the synthetic child accepts this test message
bool source_animated = true; // child fixture only; queued WM_TIMER cannot restart it
void expect(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void pump()
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
}
void await(const std::function<bool()> &condition, const char *message, ULONGLONG ms = 5000)
{
    const auto deadline = GetTickCount64() + ms;
    while (!condition()) { expect(GetTickCount64() < deadline, message); pump(); Sleep(10); }
}
LRESULT CALLBACK source_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == sequence_resize) {
        sequence_color = static_cast<COLORREF>(lparam);
        return source_proc(window, static_resize, LOWORD(wparam), HIWORD(wparam));
    }
    if (message == static_resize) {
        source_animated = false; KillTimer(window, 1);
        if (wparam < 1 || wparam > 4096 || lparam < 1 || lparam > 4096) return 0;
        if (!SetWindowPos(window, nullptr, 0, 0, static_cast<int>(wparam), static_cast<int>(lparam),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) return 0;
        return RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW) != FALSE;
    }
    if (message == animate_source) {
        source_animated = true; sequence_color = CLR_INVALID;
        return SetTimer(window, 1, 100, nullptr) != 0;
    }
    if (message == WM_TIMER) { if (source_animated) InvalidateRect(window, nullptr, FALSE); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window, &paint);
        RECT rect{}; GetClientRect(window, &rect);
        const COLORREF color = sequence_color != CLR_INVALID ? sequence_color :
            rect.right == 200 && rect.bottom == 300 ? portrait_color :
            rect.right == 600 && rect.bottom == 200 ? wide_color : video_color;
        const HBRUSH base = CreateSolidBrush(color); FillRect(dc, &rect, base); DeleteObject(base);
        rect = {0, 0, (GetTickCount64() / 100) % 2 ? 30 : 50, 10};
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        EndPaint(window, &paint); return 0;
    }
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
int source()
{
    WNDCLASSW klass{}; klass.lpfnWndProc = source_proc; klass.hInstance = GetModuleHandleW(nullptr); klass.lpszClassName = source_class;
    expect(RegisterClassW(&klass) != 0, "source class");
    HWND window = CreateWindowExW(0, source_class, L"Synthetic game", WS_POPUP | WS_VISIBLE | WS_SYSMENU | WS_MINIMIZEBOX,
        30, 30, 400, 300, nullptr, nullptr, klass.hInstance, nullptr);
    expect(window != nullptr, "source window"); SetTimer(window, 1, 100, nullptr);
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
struct Child {
    PROCESS_INFORMATION process{};
    ~Child()
    {
        if (process.hProcess) { TerminateProcess(process.hProcess, 0); WaitForSingleObject(process.hProcess, 2000); CloseHandle(process.hProcess); }
        if (process.hThread) CloseHandle(process.hThread);
    }
    Child()
    {
        wchar_t path[32768]{}; expect(GetModuleFileNameW(nullptr, path, 32768) != 0, "test executable");
        std::wstring command = L"\"" + std::wstring(path) + L"\" --source";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        expect(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE, "synthetic source process");
    }
    HWND window() const
    {
        struct Find { DWORD pid; HWND found = nullptr; } find{process.dwProcessId};
        EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            auto &f = *reinterpret_cast<Find *>(parameter); DWORD pid = 0; (void)GetWindowThreadProcessId(window, &pid);
            wchar_t name[128]{}; GetClassNameW(window, name, 128);
            if (pid == f.pid && std::wstring_view(name) == source_class) { f.found = window; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&find));
        return find.found;
    }
};
struct Window {
    HWND value = nullptr;
    ~Window() { if (value) DestroyWindow(value); }
    explicit Window(HWND window) : value(window) { expect(value != nullptr, "fixture window"); }
};
COLORREF pixel(int x, int y)
{
    const HDC dc = GetDC(nullptr); if (!dc) return CLR_INVALID;
    const auto result = GetPixel(dc, x, y); ReleaseDC(nullptr, dc); return result;
}
bool pixel_matches(COLORREF a, COLORREF b)
{
    const auto close = [](int x, int y) { return x > y ? x - y <= 8 : y - x <= 8; };
    return a != CLR_INVALID && close(GetRValue(a), GetRValue(b)) && close(GetGValue(a), GetGValue(b)) && close(GetBValue(a), GetBValue(b));
}
// Test-only evidence: fixed synthetic sample points, no screenshot, title,
// arbitrary desktop enumeration or production capture-data logging.
void first_frame_evidence(const chatview::WindowCapture &capture, HWND input, HWND output, HWND hud)
{
    const auto snapshot = capture.snapshot();
    const auto sample = pixel(660, 150);
    const auto source_sample = pixel(330, 230);
    const auto overlay_sample = pixel(150, 140);
    const HWND top = GetAncestor(WindowFromPoint({660, 150}), GA_ROOT);
    RECT source_bounds{}, output_bounds{};
    GetWindowRect(input, &source_bounds); GetWindowRect(output, &output_bounds);
    POINT cursor{}; GetCursorPos(&cursor);
    std::cerr << "Synthetic WGC first-frame evidence: status=" << static_cast<int>(snapshot.status)
        << " running=" << capture.running() << " frames=" << snapshot.frames
        << " size=" << snapshot.width << 'x' << snapshot.height
        << " received=" << snapshot.received_width << 'x' << snapshot.received_height
        << " surface=" << snapshot.surface_width << 'x' << snapshot.surface_height
        << " recreates=" << snapshot.recreates << " clipped=" << snapshot.clipped_frames
        << " hresult=" << snapshot.failure_hresult
        << " output_pixel=" << sample << " source_pixel=" << source_sample
        << " hud_pixel=" << overlay_sample << " output_on_top=" << (top == output)
        << " source_visible=" << (IsWindowVisible(input) != FALSE)
        << " source_minimized=" << (IsIconic(input) != FALSE)
        << " hud_visible=" << (IsWindowVisible(hud) != FALSE)
        << " source_rect=" << source_bounds.left << ',' << source_bounds.top << ',' << source_bounds.right << ',' << source_bounds.bottom
        << " output_rect=" << output_bounds.left << ',' << output_bounds.top << ',' << output_bounds.right << ',' << output_bounds.bottom
        << " cursor_in_source=" << (PtInRect(&source_bounds, cursor) != FALSE) << '\n';
}
void resize_evidence(const chatview::WindowCapture &capture, HWND input, HWND output, const char *phase,
                     unsigned expected_width, unsigned expected_height)
{
    const auto s = capture.snapshot();
    RECT source_bounds{}, output_bounds{};
    GetWindowRect(input, &source_bounds); GetWindowRect(output, &output_bounds);
    // Fixed synthetic points only. Observed geometry is separate from the last
    // presented geometry, so clipped growth and a presentation failure differ.
    std::cout << "Synthetic WGC resize: phase=" << phase << " expected=" << expected_width << 'x' << expected_height
        << " status=" << static_cast<int>(s.status) << " running=" << capture.running() << " frames=" << s.frames
        << " presented=" << s.width << 'x' << s.height << " received=" << s.received_width << 'x' << s.received_height
        << " surface=" << s.surface_width << 'x' << s.surface_height
        << " pool=" << s.pool_width << 'x' << s.pool_height
        << " recreates=" << s.recreates << " clipped=" << s.clipped_frames << " hresult=" << s.failure_hresult
        << " fresh=" << chatview::video_frame_fresh(GetTickCount64(), s.content_at_ms)
        << " pixels(center,top,bottom,left,right)=" << pixel(660,150) << ',' << pixel(660,40) << ','
        << pixel(660,260) << ',' << pixel(510,150) << ',' << pixel(810,150)
        << " output_on_top=" << (GetAncestor(WindowFromPoint({660,150}), GA_ROOT) == output)
        << " source_rect=" << source_bounds.left << ',' << source_bounds.top << ',' << source_bounds.right << ',' << source_bounds.bottom
        << " output_rect=" << output_bounds.left << ',' << output_bounds.top << ',' << output_bounds.right << ',' << output_bounds.bottom << '\n';
}
void resize_sequence(chatview::WindowCapture &capture, HWND input, HWND output, HWND hud)
{
    // Independent expected rectangles in the 320x240 output. Odd dimensions,
    // A -> B -> A and same-size new content must not be satisfied by an old
    // snapshot or old pixels. These are inputs, not retries of a failed test.
    const struct { unsigned width, height; RECT fit; } steps[] = {
        {200, 300, {80, 0, 240, 240}}, {600, 300, {0, 40, 320, 200}},
        {300, 500, {88, 0, 232, 240}}, {600, 200, {0, 67, 320, 173}},
        {201, 401, {100, 0, 220, 240}}, {601, 201, {0, 66, 320, 173}},
        {200, 300, {80, 0, 240, 240}}, {200, 300, {80, 0, 240, 240}},
        {600, 300, {0, 40, 320, 200}}, {200, 300, {80, 0, 240, 240}},
        {600, 200, {0, 67, 320, 173}}, {600, 300, {0, 40, 320, 200}},
    };
    unsigned index = 0;
    for (const auto &step : steps) {
        const auto before = capture.snapshot();
        const auto color = RGB(35 + 17 * index, 185 - 11 * index, 55 + 13 * index);
        const auto phase = std::string("static resize sequence ") + std::to_string(index + 1);
        if (index >= 8) {
            // Resize again before waiting for WGC/Present; the compositor may
            // coalesce these frames, but only the final generation may satisfy
            // the check. No production frame injection or timing override.
            expect(SendMessageW(input, sequence_resize, MAKELONG(201, 401), RGB(200, 40, 40)) != 0,
                "burst portrait redraw");
            expect(SendMessageW(input, sequence_resize, MAKELONG(601, 201), RGB(40, 40, 200)) != 0,
                "burst landscape redraw");
        }
        expect(SendMessageW(input, sequence_resize, MAKELONG(step.width, step.height), color) != 0,
            "single final-generation redraw, no animation timer");
        const auto pixels = [&] {
            const auto &r = step.fit;
            // Nine interior samples include all four newly exposed corners;
            // stay away from the source's small white animation marker.
            for (const auto x : {r.left + 20, (r.left + r.right) / 2, r.right - 6})
                for (const auto y : {r.top + 20, (r.top + r.bottom) / 2, r.bottom - 6})
                    if (!pixel_matches(pixel(500 + x, 30 + y), color)) return false;
            if (r.left && !pixel_matches(pixel(500 + r.left - 4, 150), RGB(0,0,0))) return false;
            if (r.right < 320 && !pixel_matches(pixel(500 + r.right + 4, 150), RGB(0,0,0))) return false;
            if (r.top && !pixel_matches(pixel(660, 30 + r.top - 4), RGB(0,0,0))) return false;
            if (r.bottom < 240 && !pixel_matches(pixel(660, 30 + r.bottom + 4), RGB(0,0,0))) return false;
            return true;
        };
        try {
            await([&] {
                const auto s = capture.snapshot();
                expect(capture.running() && s.status != chatview::WindowCaptureStatus::Failed &&
                    s.status != chatview::WindowCaptureStatus::SourceLost, "resize must not terminate this capture");
                return s.status == chatview::WindowCaptureStatus::Capturing && s.frames > before.frames &&
                    s.width == step.width && s.height == step.height &&
                    chatview::video_frame_fresh(GetTickCount64(), s.content_at_ms) && pixels();
            }, phase.c_str()); // unchanged five-second wait, RGB tolerance 8
        } catch (...) {
            std::cout << "Synthetic WGC generation: index=" << index + 1 << " expected_color=" << color << '\n';
            resize_evidence(capture, input, output, phase.c_str(), step.width, step.height); throw;
        }
        const auto after = capture.snapshot();
        const bool growth = step.width > before.pool_width || step.height > before.pool_height;
        expect(after.pool_width == (step.width > before.pool_width ? step.width : before.pool_width) &&
            after.pool_height == (step.height > before.pool_height ? step.height : before.pool_height),
            "capture pool retains bounded per-axis capacity after shrink and mixed resize");
        expect(after.recreates == before.recreates + (growth ? 1U : 0U),
            "resize reuses existing capacity and never recreates again for a late surface");
        resize_evidence(capture, input, output, phase.c_str(), step.width, step.height);
        expect(IsWindowVisible(hud) && IsWindow(output), "same output and visible HUD survive every resize");
        ++index;
    }
    const auto before = capture.snapshot().frames;
    expect(SendMessageW(input, animate_source, 0, 0) != 0, "resume animation after rapid resize");
    await([&] {
        const auto s = capture.snapshot();
        return s.frames > before && s.width == 600 && s.height == 300 &&
            chatview::video_frame_fresh(GetTickCount64(), s.content_at_ms) &&
            pixel_matches(pixel(660, 150), video_color) && pixel_matches(pixel(660, 40), RGB(0,0,0));
    }, "original animated content continues without capture restart");
}
void exercise()
{
    expect(GetSystemMetrics(SM_CXSCREEN) >= 900 && GetSystemMetrics(SM_CYSCREEN) >= 600, "interactive desktop required (not skipped)");
    Child game; HWND input = nullptr;
    await([&] { input = game.window(); return input && IsWindowVisible(input); }, "source ready");
    WNDCLASSW klass{}; klass.lpfnWndProc = DefWindowProcW; klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = L"ChatView.VideoPixelFixture"; klass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    expect(RegisterClassW(&klass) != 0, "output class");
    Window output(CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOPMOST, klass.lpszClassName, L"Synthetic output", WS_POPUP | WS_VISIBLE,
        500, 30, 320, 240, nullptr, nullptr, klass.hInstance, nullptr));
    // The HUD-like window is deliberately NOT capture-excluded. WGC must select
    // the game surface, not desktop pixels underneath/around it.
    Window hud(CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP | WS_VISIBLE | SS_WHITERECT,
        100, 100, 150, 80, nullptr, nullptr, klass.hInstance, nullptr));
    SetWindowLongPtrW(hud.value, GWL_STYLE, WS_POPUP | WS_VISIBLE | SS_OWNERDRAW);
    const auto draw_hud = [&] {
        HDC dc = GetDC(hud.value); RECT rect{}; GetClientRect(hud.value, &rect);
        HBRUSH brush = CreateSolidBrush(hud_color); FillRect(dc, &rect, brush); DeleteObject(brush); ReleaseDC(hud.value, dc);
    };
    pump(); draw_hud();
    chatview::WindowCapture capture;
    expect(!capture.start(hud.value, output.value), "self/HUD capture rejected");
    expect(!capture.start(GetDesktopWindow(), output.value), "desktop is never a window fallback");
    expect(capture.start(input, output.value), "start actual window capture");
    expect(!capture.start(input, output.value), "duplicate start rejected");
    try {
        await([&] { draw_hud(); return capture.snapshot().frames >= 2 && pixel_matches(pixel(660, 150), video_color); }, "real WGC output excludes overlaid HUD", 10000);
    } catch (...) {
        first_frame_evidence(capture, input, output.value, hud.value); throw;
    }
    expect(IsWindowVisible(hud.value) && pixel_matches(pixel(150, 140), hud_color), "HUD remains locally visible while output is clean");
    // Reuse a literal BLACK_BRUSH class, as the production panel does. The
    // old SS_BLACKRECT control instead followed the current window-frame color.
    // Keep the RGB(0,0,0) assertion: masking is independent of theme and GPU.
    Window cover(CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, klass.lpszClassName, L"",
        WS_POPUP | WS_VISIBLE, 500, 30, 320, 240,
        output.value, nullptr, klass.hInstance, nullptr));
    await([&] { return pixel_matches(pixel(660, 150), RGB(0,0,0)); }, "independent black cover masks active GPU output");
    ShowWindow(cover.value, SW_HIDE);
    await([&] { return pixel_matches(pixel(660, 150), video_color); }, "unmask reveals fresh game pixels");
    expect(capture.snapshot().width == 400 && capture.snapshot().height == 300, "captured content dimensions");
    const auto verify_resize = [&](unsigned width, unsigned height, std::uint64_t previous_frames,
                                   const char *phase, const std::function<bool()> &pixels) {
        try {
            await([&] {
                const auto value = capture.snapshot();
                return capture.running() && value.status == chatview::WindowCaptureStatus::Capturing &&
                    value.width == width && value.height == height && value.frames > previous_frames &&
                    chatview::video_frame_fresh(GetTickCount64(), value.content_at_ms) && pixels();
            }, phase); // original five-second deadline and RGB tolerance 8
        } catch (...) { resize_evidence(capture, input, output.value, phase, width, height); throw; }
        resize_evidence(capture, input, output.value, phase, width, height);
    };
    auto previous_frames = capture.snapshot().frames;
    expect(SetWindowPos(input, nullptr, 0, 0, 600, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "grow source window");
    verify_resize(600, 300, previous_frames, "resize recreates pool and letterboxes without old padding", [&] {
        // Preserve the exact #353 center/top assertion; also check the opposite
        // bar and the newly exposed far-right part of the wider content.
        return pixel_matches(pixel(660, 150), video_color) && pixel_matches(pixel(660, 40), RGB(0,0,0)) &&
            pixel_matches(pixel(660, 260), RGB(0,0,0)) && pixel_matches(pixel(800, 150), video_color);
    });
    previous_frames = capture.snapshot().frames;
    expect(SendMessageW(input, static_resize, 200, 300) != 0, "single static source shrink");
    verify_resize(200, 300, previous_frames, "static shrink displays complete frame and replaces horizontal bars", [&] {
        return pixel_matches(pixel(660, 150), portrait_color) && pixel_matches(pixel(660, 40), portrait_color) &&
            pixel_matches(pixel(660, 260), portrait_color) && pixel_matches(pixel(510, 150), RGB(0,0,0)) &&
            pixel_matches(pixel(810, 150), RGB(0,0,0));
    });
    previous_frames = capture.snapshot().frames;
    expect(SendMessageW(input, animate_source, 0, 0) != 0, "resume source animation without restarting capture");
    verify_resize(200, 300, previous_frames, "new frames continue after static shrink", [&] {
        return pixel_matches(pixel(660, 150), portrait_color);
    });
    previous_frames = capture.snapshot().frames;
    expect(SetWindowPos(input, nullptr, 0, 0, 600, 200, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "mixed growth and shrink");
    verify_resize(600, 200, previous_frames, "mixed resize replaces old pillarbox with new wide content", [&] {
        return pixel_matches(pixel(660, 150), wide_color) && pixel_matches(pixel(660, 40), RGB(0,0,0)) &&
            pixel_matches(pixel(660, 260), RGB(0,0,0)) && pixel_matches(pixel(510, 150), wide_color) &&
            pixel_matches(pixel(810, 150), wide_color);
    });
    previous_frames = capture.snapshot().frames;
    expect(SetWindowPos(input, nullptr, 0, 0, 600, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "restore landscape input");
    verify_resize(600, 300, previous_frames, "height growth continues on the same capture", [&] {
        return pixel_matches(pixel(660, 150), video_color) && pixel_matches(pixel(660, 40), RGB(0,0,0)) &&
            pixel_matches(pixel(660, 85), video_color) && pixel_matches(pixel(660, 215), video_color);
    });
    resize_sequence(capture, input, output.value, hud.value);
    SetWindowPos(output.value, nullptr, 0, 0, 300, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    await([&] { return pixel_matches(pixel(650, 180), video_color) && pixel_matches(pixel(650, 40), RGB(0,0,0)); }, "output resizing preserves aspect and black bars");
    ShowWindow(input, SW_MINIMIZE);
    await([&] { return !capture.running(); }, "minimized source stops capture", 2000);
    expect(capture.snapshot().status == chatview::WindowCaptureStatus::SourceLost, "minimize is source loss, no desktop fallback");
    await([&] { return pixel_matches(pixel(650, 180), RGB(0,0,0)); }, "source loss clears output");
    ShowWindow(input, SW_RESTORE);
    expect(capture.start(input, output.value), "explicit restart after source restoration");
    await([&] { return capture.snapshot().frames >= 2 && pixel_matches(pixel(650, 180), video_color); }, "restart displays fresh frames");
    const auto before = GetTickCount64(); capture.stop();
    expect(GetTickCount64() - before < 200, "UI stop never waits on capture/GPU");
    await([&] { return !capture.running(); }, "capture worker cancellation", 2000);
    await([&] { return pixel_matches(pixel(650, 180), RGB(0,0,0)); }, "cancel clears stale pixels");
    expect(IsWindowVisible(hud.value), "capture stop does not hide HUD");
    expect(capture.start(input, output.value), "start before source close");
    await([&] { return capture.snapshot().frames >= 1; }, "frame before close");
    PostMessageW(input, WM_CLOSE, 0, 0);
    await([&] { return !capture.running() && pixel_matches(pixel(650, 180), RGB(0,0,0)); }, "closed source clears output", 2000);
    capture.close();
    std::cout << "WGC window -> GPU output: overlay isolation, resize, minimize, stop, restart and close passed\n";
}
}
int main(int argc, char **argv)
{
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (argc == 2 && std::string_view(argv[1]) == "--source") return source();
        expect(argc == 1, "unexpected test arguments"); exercise(); return 0;
    } catch (const std::exception &error) { std::cerr << "Window capture test: " << error.what() << '\n'; return 1; }
}
