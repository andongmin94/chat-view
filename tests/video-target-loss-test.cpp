// SPDX-License-Identifier: GPL-2.0-or-later
// Actual HUD, selection controls, WGC worker and black output HWNDs. Only the
// output rectangle/consent boundary is synthetic, as in the lifecycle test.
// This does not certify physical extended monitors, cabling or receiver video.
#include "hud/video-output-panel.hpp"
#include "hud/hud-window.hpp"
#include "common/win32-handle.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chatview {
struct VideoOutputPanelTestAccess {
    static HWND open(VideoOutputPanel &p) { p.open(); return p.panel_; }
    static HWND output(VideoOutputPanel &p) { return p.output_; }
    static HWND cover(VideoOutputPanel &p) { return p.cover_; }
    static auto &capture(VideoOutputPanel &p) { return p.capture_; }
    static bool requested(VideoOutputPanel &p) { return p.requested_; }
    static void consume(VideoOutputPanel &p, const WindowCaptureSnapshot &s) { p.update_capture(s); }
    static bool retired(VideoOutputPanel &p) {
        return !p.requested_ && !p.source_ && !p.source_process_ && !p.source_thread_ &&
            !p.source_monitor_ && p.sources_.empty() && p.monitors_.empty() && !p.check_.active();
    }
    static bool listed(VideoOutputPanel &p, HWND window) {
        for (const auto &source : p.sources_) if (source.window == window) return true;
        return false;
    }
    static void pattern(VideoOutputPanel &p, HWND source) {
        // Only test friendship supplies this one-desktop rectangle. No runtime
        // flag disables production topology validation or its default-NO dialog.
        p.source_ = source;
        p.source_thread_ = GetWindowThreadProcessId(source, &p.source_process_);
        p.source_monitor_ = MonitorFromWindow(source, MONITOR_DEFAULTTONULL);
        p.output_bounds_ = {500, 30, 820, 270}; p.ensure_output();
        p.monitor_ = MonitorFromWindow(p.output_, MONITOR_DEFAULTTONULL);
        p.sources_.clear(); p.monitors_.clear();
        SendDlgItemMessageW(p.panel_, 201, CB_RESETCONTENT, 0, 0);
        SendDlgItemMessageW(p.panel_, 202, CB_RESETCONTENT, 0, 0);
        p.sources_.push_back({source, p.source_process_, p.source_thread_, L"Synthetic game"});
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        p.monitors_.push_back({p.monitor_, info});
        SendDlgItemMessageW(p.panel_, 201, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Synthetic game"));
        SendDlgItemMessageW(p.panel_, 202, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Synthetic output"));
        SendDlgItemMessageW(p.panel_, 201, CB_SETCURSEL, 0, 0);
        SendDlgItemMessageW(p.panel_, 202, CB_SETCURSEL, 0, 0);
        ++p.selection_epoch_;
        p.show_pattern();
        KillTimer(p.cover_, 0x435650); // a synthetic rectangle is not a real extended monitor
    }
    static bool confirm(VideoOutputPanel &p) {
        // Only topology/human consent is synthetic; production consumes the check.
        p.begin_capture(); return p.requested_;
    }
};
}
namespace {
using Access = chatview::VideoOutputPanelTestAccess;
void expect(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void pump()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        expect(message.message != WM_QUIT, "no unexpected quit");
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
void await(const std::function<bool()> &condition, const char *message, ULONGLONG ms = 5000)
{
    const auto deadline = GetTickCount64() + ms;
    while (!condition()) { expect(GetTickCount64() < deadline, message); pump(); Sleep(10); }
}
HWND find(DWORD pid, const wchar_t *name)
{
    struct Query { DWORD pid; const wchar_t *name; HWND result = nullptr; } query{pid, name};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto &q = *reinterpret_cast<Query *>(data);
        DWORD process = 0; GetWindowThreadProcessId(window, &process);
        wchar_t klass[128]{}; GetClassNameW(window, klass, 128);
        if (process == q.pid && std::wstring_view(klass) == q.name) { q.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.result;
}
struct Source {
    chatview::UniqueHandle process;
    DWORD pid = 0;
    HWND window = nullptr;
    explicit Source(const wchar_t *path) {
        std::wstring command = L"\"" + std::wstring(path) + L"\" --source";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION info{};
        expect(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info) != FALSE,
            "create separate synthetic source process");
        process.reset(info.hProcess); CloseHandle(info.hThread); pid = info.dwProcessId;
    }
    void ready() { await([&] {
        window = find(pid, L"ChatView.SyntheticVideoSource"); return window && IsWindowVisible(window);
    }, "source window ready"); }
    ~Source() {
        if (process && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.get(), 99); WaitForSingleObject(process.get(), 2000);
        }
    }
};
bool pixel(COLORREF expected)
{
    HDC dc = GetDC(nullptr); if (!dc) return false;
    const auto actual = GetPixel(dc, 660, 150); ReleaseDC(nullptr, dc);
    const auto near_color = [](int a, int b) { return a >= b - 8 && a <= b + 8; };
    return actual != CLR_INVALID && near_color(GetRValue(actual), GetRValue(expected)) &&
        near_color(GetGValue(actual), GetGValue(expected)) && near_color(GetBValue(actual), GetBValue(expected));
}
void start(chatview::VideoOutputPanel &panel, HWND source)
{
    Access::pattern(panel, source);
    expect(!Access::capture(panel).running(), "new pattern does not capture the replacement window");
    expect(Access::confirm(panel), "new explicit confirmation starts the selected capture");
    expect(IsWindowVisible(Access::cover(panel)), "first frame starts covered");
    await([&] {
        auto value = Access::capture(panel).snapshot();
        Access::consume(panel, value);
        return value.frames >= 2 && !IsWindowVisible(Access::cover(panel)) && pixel(RGB(20, 100, 180));
    }, "fresh selected WGC frames are displayed", 10000);
}
void retired(chatview::VideoOutputPanel &panel, HWND controls, HWND hud, HWND output, HWND cover)
{
    expect(Access::retired(panel), "terminal capture retires identity, choices and confirmation");
    expect(Access::output(panel) == output && Access::cover(panel) == cover && IsWindowVisible(cover),
        "same output and black cover survive target loss");
    expect(SendDlgItemMessageW(controls, 201, CB_GETCOUNT, 0, 0) == 0 &&
        SendDlgItemMessageW(controls, 202, CB_GETCOUNT, 0, 0) == 0 && !IsWindowEnabled(GetDlgItem(controls, 204)),
        "controls match the re-selection instruction");
    await([&] { return !Access::capture(panel).running() && pixel(RGB(0, 0, 0)); }, "cancel drains under black output", 2000);
    expect(IsWindowVisible(hud), "private HUD stays locally visible");
}
void exercise(const wchar_t *path)
{
    expect(GetSystemMetrics(SM_CXSCREEN) >= 900 && GetSystemMetrics(SM_CYSCREEN) >= 600, "interactive desktop required, not skipped");
    Source first(path); first.ready();
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "production HUD creation"); hud.show_ready();
    await([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "production HUD ready", 20000);
    const HWND hud_window = find(GetCurrentProcessId(), L"ChatViewObsHudWindow");
    expect(hud_window != nullptr, "find production HUD");
    SetWindowPos(hud_window, nullptr, 40, 40, 300, 220, SWP_NOZORDER | SWP_NOACTIVATE);
    chatview::VideoOutputPanel panel(hud, true);
    const HWND controls = Access::open(panel); expect(controls != nullptr, "existing selection controls");
    SetWindowPos(controls, nullptr, 20, 350, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    start(panel, first.window);
    const HWND output = Access::output(panel), cover = Access::cover(panel);
    auto late = Access::capture(panel).snapshot();
    expect(PostMessageW(first.window, WM_CLOSE, 0, 0) != FALSE, "close actual captured window");
    await([&] { return !Access::capture(panel).running(); }, "actual source close ends WGC", 2000);
    expect(Access::capture(panel).snapshot().status == chatview::WindowCaptureStatus::SourceLost, "real source-loss report");
    Access::consume(panel, Access::capture(panel).snapshot());
    retired(panel, controls, hud_window, output, cover);
    Source replacement(path); replacement.ready(); // same caption, distinct process
    expect(first.pid != replacement.pid, "replacement is a different process with the same title");
    late.content_at_ms = GetTickCount64(); Access::consume(panel, late);
    panel.tick();
    expect(Access::retired(panel) && pixel(RGB(0, 0, 0)), "replacement and late fresh status cannot automatically resume");
    SendMessageW(controls, WM_COMMAND, 204, 0);
    expect(!Access::requested(panel) && !Access::confirm(panel), "old start or confirmation cannot be reused");
    SendMessageW(controls, WM_COMMAND, 203, 0);
    expect(Access::listed(panel, replacement.window) &&
        SendDlgItemMessageW(controls, 201, CB_GETCURSEL, 0, 0) == CB_ERR &&
        SendDlgItemMessageW(controls, 202, CB_GETCURSEL, 0, 0) == CB_ERR,
        "refresh finds the replacement but does not select either target");
    expect(IsWindowVisible(cover) && pixel(RGB(0, 0, 0)), "refresh does not remove black output");
    start(panel, replacement.window);
    expect(Access::output(panel) == output && Access::cover(panel) == cover, "explicit replacement reuses output HWNDs");
    // Deterministic terminal-before-worker-finished case. Real WGC remains
    // alive until the production UI consumer requests its cancellation.
    auto failure = Access::capture(panel).snapshot(); failure.status = chatview::WindowCaptureStatus::Failed;
    expect(Access::capture(panel).running(), "real capture active before injected terminal report");
    Access::consume(panel, failure);
    retired(panel, controls, hud_window, output, cover);
    Access::pattern(panel, GetDesktopWindow());
    expect(!Access::confirm(panel), "rejected start does not pretend to await a frame");
    retired(panel, controls, hud_window, output, cover);
    SendMessageW(controls, WM_CLOSE, 0, 0);
    expect(!IsWindowVisible(controls) && Access::open(panel) == controls && Access::retired(panel),
        "close/reopen retains the same stopped selection panel");
    expect(IsWindow(output) && pixel(RGB(0, 0, 0)), "panel reopen does not release output");
    panel.close(); hud.destroy();
    std::cout << "Capture target-loss flow: actual close, choice retirement, late status, same-title replacement, explicit pattern, failed start and black HWND reuse passed\n";
}
}
int wmain(int argc, wchar_t **argv)
{
    try {
        expect(argc == 2, "synthetic source executable required");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        wchar_t temporary[MAX_PATH]{}; expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary profile root");
        const auto profile = std::wstring(temporary) + L"ChatView-TargetLoss-" + std::to_wstring(GetCurrentProcessId());
        expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE && SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str()) != FALSE,
            "isolated HUD profile");
        expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization");
        try { exercise(argv[1]); } catch (...) { CoUninitialize(); throw; }
        CoUninitialize(); return 0;
    } catch (const std::exception &error) { std::cerr << "Target-loss flow: " << error.what() << '\n'; return 1; }
}
