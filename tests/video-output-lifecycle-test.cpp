// SPDX-License-Identifier: GPL-2.0-or-later
// Real production panel, WGC/GPU and HWNDs. Mount a synthetic output rectangle
// through test friendship: there is no production topology override. Injected
// Windows notifications are not an actual lock, hotplug or two-PC recording.
#include "hud/video-output-panel.hpp"
#include "hud/video-output-pattern.hpp"
#include "hud/hud-window.hpp"
#include "common/win32-handle.hpp"
#include <wtsapi32.h>
#include <dbt.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace chatview {
struct VideoOutputPanelTestAccess {
    static HWND open(VideoOutputPanel &p) { p.open(); return p.panel_; }
    static HWND output(VideoOutputPanel &p) { return p.output_; }
    static HWND cover(VideoOutputPanel &p) { return p.cover_; }
    static bool requested(VideoOutputPanel &p) { return p.requested_; }
    static bool registered(VideoOutputPanel &p) { return p.notifications_; }
    static bool releasing(VideoOutputPanel &p) { return p.releasing_; }
    static bool choices_empty(VideoOutputPanel &p) { return p.sources_.empty() && p.monitors_.empty(); }
    static auto &capture(VideoOutputPanel &p) { return p.capture_; }
    static void place(VideoOutputPanel &p, HWND source) {
        p.output_bounds_ = {500, 30, 820, 270};
        p.source_ = source;
        p.source_thread_ = GetWindowThreadProcessId(source, &p.source_process_);
        p.source_monitor_ = MonitorFromWindow(source, MONITOR_DEFAULTTONULL);
        p.ensure_output(); // production window + owned black cover, not copies
        p.monitor_ = MonitorFromWindow(p.output_, MONITOR_DEFAULTTONULL);
    }
    static void mount(VideoOutputPanel &p, HWND source) {
        place(p, source);
        p.requested_ = p.capture_.start(source, p.output_);
        if (!p.requested_) throw std::runtime_error("mount synthetic output");
    }
    static auto &check(VideoOutputPanel &p) { return p.check_; }
    static void pattern(VideoOutputPanel &p, HWND source) {
        place(p, source);
        p.show_pattern();
        // This rectangle is not a physical extended monitor. Stop the automatic
        // topology timer, not a production guard; explicit notifications and
        // timer-after-stop tests below still run through the production WndProc.
        KillTimer(p.cover_, 0x435650);
    }
    static bool accept_for_fixture(VideoOutputPanel &p) {
        if (!p.check_.confirm(GetTickCount64(), p.selection_epoch_)) return false;
        p.begin_capture(); return p.requested_;
    }
    static void expire(VideoOutputPanel &p) {
        const auto now = GetTickCount64();
        const auto start = now >= VideoOutputCheck::lifetime_ms ? now - VideoOutputCheck::lifetime_ms : now + 1;
        p.check_.begin(start, p.selection_epoch_, p.check_.identifier()); p.check_.painted(true);
    }
    static void reveal(VideoOutputPanel &p) { ShowWindow(p.cover_, SW_HIDE); }
    static void reuse(VideoOutputPanel &p) { p.ensure_output(); }
    static void seed_choices(VideoOutputPanel &p) {
        p.sources_.push_back({p.source_, p.source_process_, p.source_thread_, L"Synthetic source"});
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        p.monitors_.push_back({nullptr, info});
        SendDlgItemMessageW(p.panel_, 201, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Synthetic source"));
        SendDlgItemMessageW(p.panel_, 202, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Synthetic different output"));
        SendDlgItemMessageW(p.panel_, 201, CB_SETCURSEL, 0, 0);
        SendDlgItemMessageW(p.panel_, 202, CB_SETCURSEL, 0, 0);
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
        expect(message.message != WM_QUIT, "no unexpected process exit");
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
void await(const std::function<bool()> &condition, const char *message, ULONGLONG ms = 5000)
{
    const auto deadline = GetTickCount64() + ms;
    while (!condition()) { expect(GetTickCount64() < deadline, message); pump(); Sleep(10); }
}
HWND find(DWORD process, const wchar_t *title, const wchar_t *klass = nullptr)
{
    struct Query { DWORD process; const wchar_t *title; const wchar_t *klass; HWND found = nullptr; } query{process, title, klass};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto &q = *reinterpret_cast<Query *>(parameter); DWORD pid = 0;
        (void)GetWindowThreadProcessId(window, &pid);
        if (pid != q.process) return TRUE;
        wchar_t value[256]{};
        if (q.klass) GetClassNameW(window, value, 256); else GetWindowTextW(window, value, 256);
        if (std::wstring_view(value) == (q.klass ? q.klass : q.title)) { q.found = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.found;
}
class Source final {
public:
    chatview::UniqueHandle process;
    DWORD pid = 0;
    explicit Source(const wchar_t *path)
    {
        std::wstring command = L"\"" + std::wstring(path) + L"\" --source";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        expect(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
            &startup, &info) != FALSE, "launch existing synthetic window fixture");
        process.reset(info.hProcess); CloseHandle(info.hThread); pid = info.dwProcessId;
    }
    ~Source() { if (WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT) {
        TerminateProcess(process.get(), 99); WaitForSingleObject(process.get(), 2000);
    } }
};
bool cyan_bar()
{
    const HDC dc = GetDC(nullptr); if (!dc) return false;
    const auto value = GetPixel(dc, 604, 75); ReleaseDC(nullptr, dc);
    return value != CLR_INVALID && GetRValue(value) <= 8 && GetGValue(value) >= 184 &&
        GetGValue(value) <= 200 && GetBValue(value) >= 184 && GetBValue(value) <= 200;
}
bool black()
{
    const HDC dc = GetDC(nullptr); if (!dc) return false;
    const auto value = GetPixel(dc, 660, 150); ReleaseDC(nullptr, dc);
    return value != CLR_INVALID && GetRValue(value) <= 8 && GetGValue(value) <= 8 && GetBValue(value) <= 8;
}
bool video()
{
    const HDC dc = GetDC(nullptr); if (!dc) return false;
    const auto value = GetPixel(dc, 660, 150); ReleaseDC(nullptr, dc);
    const auto within_tolerance = [](int a, int b) { return a >= b - 8 && a <= b + 8; };
    return value != CLR_INVALID && within_tolerance(GetRValue(value), 20) && within_tolerance(GetGValue(value), 100) && within_tolerance(GetBValue(value), 180);
}
void start_pixels(chatview::VideoOutputPanel &panel, HWND source)
{
    Access::mount(panel, source);
    await([&] { return Access::capture(panel).snapshot().frames >= 2; }, "real WGC frames", 10000);
    Access::reveal(panel);
    await(video, "synthetic game pixels on production output");
}
void stopped(chatview::VideoOutputPanel &panel, HWND hud)
{
    expect(!Access::requested(panel), "notification latched before outer loop tick");
    expect(IsWindowVisible(Access::cover(panel)), "black cover shown synchronously");
    await([&] { return !Access::capture(panel).running() && black(); }, "worker stops under black cover", 2000);
    panel.tick();
    expect(IsWindowVisible(hud), "video interruption does not hide local HUD");
}
void release_answer(chatview::VideoOutputPanel &panel, int answer, bool change_during_consent = false)
{
    const HWND owner = Access::open(panel);
    std::exception_ptr failure;
    std::thread responder([&] {
        HWND dialog = nullptr;
        try {
            const auto deadline = GetTickCount64() + 5000;
            while (!(dialog = find(GetCurrentProcessId(), L"ChatView · 출력 창 해제 확인"))) {
                expect(GetTickCount64() < deadline, "release confirmation appears"); Sleep(10);
            }
            const HWND decline = GetDlgItem(dialog, IDNO);
            expect(decline && (GetWindowLongPtrW(decline, GWL_STYLE) & BS_TYPEMASK) == BS_DEFPUSHBUTTON,
                "release defaults to keeping black output");
            if (change_during_consent) {
                SendMessageW(owner, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0);
                SendMessageW(owner, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0);
            }
        } catch (...) { failure = std::current_exception(); }
        if (dialog) PostMessageW(dialog, WM_COMMAND, static_cast<WPARAM>(failure ? IDNO : answer), 0);
    });
    SendMessageW(owner, WM_COMMAND, 206, 0);
    responder.join();
    if (failure) std::rethrow_exception(failure);
}
void pattern_pixels()
{
    const HDC dc = CreateCompatibleDC(nullptr);
    expect(dc != nullptr, "pattern memory DC");
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 320; info.bmiHeader.biHeight = -240;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    const HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) { DeleteDC(dc); throw std::runtime_error("pattern bitmap"); }
    const HGDIOBJ old = SelectObject(dc, bitmap);
    try {
        expect(old && old != HGDI_ERROR, "select pattern bitmap");
        expect(chatview::paint_video_output_pattern(dc, {0, 0, 320, 240}, 0xabcdefU, 0), "first pattern paint");
        expect(GetPixel(dc, 104, 45) == RGB(0,192,192), "synthetic cyan bar");
        expect(GetPixel(dc, 20, 5) == RGB(255,255,255), "full output border");
        expect(GetPixel(dc, 20, 210) == RGB(255,255,255) && GetPixel(dc, 60, 210) == RGB(0,0,0), "first motion cell");
        expect(chatview::paint_video_output_pattern(dc, {0, 0, 320, 240}, 0xabcdefU, 1), "next pattern paint");
        expect(GetPixel(dc, 20, 210) == RGB(0,0,0) && GetPixel(dc, 60, 210) == RGB(255,255,255), "motion advances and clears previous cell");
        expect(!chatview::paint_video_output_pattern(dc, {0, 0, 0, 240}, 1, 0), "invalid geometry fails closed");
    } catch (...) {
        if (old && old != HGDI_ERROR) SelectObject(dc, old);
        DeleteObject(bitmap); DeleteDC(dc); throw;
    }
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
}
void exercise(const wchar_t *source_executable)
{
    pattern_pixels();
    expect(GetSystemMetrics(SM_CXSCREEN) >= 900 && GetSystemMetrics(SM_CYSCREEN) >= 600,
        "interactive desktop required, not skipped");
    Source source(source_executable); HWND game = nullptr;
    await([&] { game = find(source.pid, nullptr, L"ChatView.SyntheticVideoSource"); return game && IsWindowVisible(game); }, "source ready");
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "create production HUD"); hud.show_ready();
    await([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "HUD readiness", 20000);
    const HWND hud_window = find(GetCurrentProcessId(), L"ChatView HUD");
    expect(hud_window != nullptr, "visible HUD");
    SetWindowPos(hud_window, nullptr, 40, 40, 300, 220, SWP_NOZORDER | SWP_NOACTIVATE);
    chatview::VideoOutputPanel panel(hud, true);
    const HWND controls = Access::open(panel);
    expect(controls && Access::registered(panel), "real controls register session notifications");
    SetWindowPos(controls, nullptr, 20, 350, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    expect(!IsWindowEnabled(GetDlgItem(controls, 204)), "game conversion is disabled before a pattern is painted");
    SendMessageW(controls, WM_COMMAND, 204, 0);
    SendMessageW(controls, WM_COMMAND, 208, 0);
    expect(!Access::capture(panel).running() && !Access::output(panel), "neither command can bypass explicit selections");
    Access::pattern(panel, game);
    expect(!Access::capture(panel).running() && Access::capture(panel).snapshot().frames == 0,
        "pattern preparation does not start WGC or read a game frame");
    await(cyan_bar, "synthetic pattern pixels on the actual independent cover");
    const auto label = chatview::video_check_label(Access::check(panel).identifier());
    wchar_t notice[512]{}; GetDlgItemTextW(controls, 207, notice, 512);
    expect(std::wstring_view(notice).find(label.data()) != std::wstring_view::npos,
        "same read-only visual identifier appears in the panel");
    expect(IsWindowEnabled(GetDlgItem(controls, 204)), "conversion enabled only after local painting");
    const HWND checked_output = Access::output(panel), checked_cover = Access::cover(panel);
    expect(Access::accept_for_fixture(panel), "fresh local check consumed before capture transition");
    expect(!Access::check(panel).active() && !IsWindowEnabled(GetDlgItem(controls, 204)) &&
        Access::output(panel) == checked_output && Access::cover(panel) == checked_cover,
        "transition consumes check and preserves both HWNDs");
    expect(IsWindowVisible(checked_cover), "transition keeps a black cover until fresh WGC frames");
    await([&] { return Access::capture(panel).snapshot().frames >= 2; }, "WGC begins only after confirmation", 10000);
    Access::reveal(panel); await(video, "pattern converts to selected game pixels");
    expect(IsWindowVisible(hud_window), "visual check and game conversion preserve private HUD");
    SendMessageW(controls, WM_COMMAND, 205, 0); stopped(panel, hud_window);
    expect(!Access::accept_for_fixture(panel), "confirmation is not reusable after stop");
    Access::pattern(panel, game);
    SendMessageW(controls, WM_COMMAND, MAKEWPARAM(201, CBN_SELCHANGE), 0);
    stopped(panel, hud_window);
    expect(!Access::check(panel).active(), "selection change invalidates the visual check");
    SendMessageW(checked_cover, WM_TIMER, 0x435650, 0);
    expect(black() && !Access::check(panel).active(), "queued timer cannot resurrect a stopped pattern");
    Access::pattern(panel, game); Access::expire(panel); panel.tick();
    expect(!Access::check(panel).active() && !Access::requested(panel) && black(), "expired check masks output without starting capture");
    const struct { UINT message; WPARAM parameter; UINT recovery; WPARAM recovered; } events[] = {
        {WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK},
        {WM_POWERBROADCAST, PBT_APMSUSPEND, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC},
        {WM_DISPLAYCHANGE, 0, 0, 0},
        {WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0, 0},
    };
    for (const auto &event : events) {
        Access::pattern(panel, game);
        SendMessageW(controls, event.message, event.parameter, 0);
        stopped(panel, hud_window);
        expect(!Access::check(panel).active(), "notification invalidates pre-capture visual check");
        if (event.recovery) SendMessageW(controls, event.recovery, event.recovered, 0);
        SendMessageW(controls, WM_COMMAND, 204, 0);
        expect(!Access::capture(panel).running() && black(), "recovery cannot start a previously checked source");
        start_pixels(panel, game); Access::seed_choices(panel);
        SendMessageW(controls, event.message, event.parameter, 0);
        stopped(panel, hud_window);
        expect(Access::choices_empty(panel) && SendDlgItemMessageW(controls, 201, CB_GETCOUNT, 0, 0) == 0 &&
            SendDlgItemMessageW(controls, 202, CB_GETCOUNT, 0, 0) == 0, "interruption clears stale target choices");
        if (event.recovery) SendMessageW(controls, event.recovery, event.recovered, 0);
        panel.tick();
        expect(!Access::requested(panel) && !Access::capture(panel).running() && black(), "recovery cannot revive old capture");
    }
    const HWND output = Access::output(panel), cover = Access::cover(panel);
    Access::reuse(panel);
    expect(Access::output(panel) == output && Access::cover(panel) == cover && black(), "restart reuses both black HWNDs without desktop gap");
    Access::seed_choices(panel);
    SendMessageW(controls, WM_COMMAND, 208, 0);
    expect(Access::output(panel) == output && !Access::requested(panel) && black(), "retarget cannot implicitly release existing output");
    start_pixels(panel, game);
    release_answer(panel, IDNO);
    stopped(panel, hud_window);
    expect(IsWindow(output) && !Access::releasing(panel), "declined release retains black output");
    release_answer(panel, IDYES, true);
    panel.tick();
    expect(IsWindow(output) && !Access::releasing(panel) && black(), "lock/unlock during consent invalidates release");
    SendMessageW(controls, WM_CLOSE, 0, 0);
    expect(!IsWindowVisible(controls) && IsWindow(output) && black(), "closing controls retains black output");
    expect(Access::open(panel) == controls && !Access::requested(panel), "reopening controls never restarts video");
    release_answer(panel, IDYES);
    panel.tick();
    expect(!IsWindow(output) && !Access::output(panel) && !Access::cover(panel), "confirmed release occurs after worker completion");
    // Final close during active GPU work must keep servicing sent DXGI messages.
    start_pixels(panel, game);
    const HWND final_output = Access::output(panel);
    panel.close();
    expect(!Access::capture(panel).running() && !IsWindow(final_output) && !Access::registered(panel), "active final close joins before HWND teardown and unregisters notifications");
    hud.destroy();
    std::cout << "Production video lifecycle: pattern/confirmation, lock/resume, power, display/device invalidation, HWND reuse, retarget rejection, release consent and active close passed\n";
}
}
int wmain(int argc, wchar_t **argv)
{
    try {
        expect(argc == 2, "synthetic-source executable required");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        wchar_t temporary[MAX_PATH]{};
        expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary profile root");
        const auto profile = std::wstring(temporary) + L"ChatView-VideoLifecycle-" + std::to_wstring(GetCurrentProcessId());
        expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE &&
            SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str()) != FALSE, "isolated HUD profile");
        expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization");
        try { exercise(argv[1]); } catch (...) { CoUninitialize(); throw; }
        CoUninitialize(); return 0;
    } catch (const std::exception &error) { std::cerr << "Video lifecycle test: " << error.what() << '\n'; return 1; }
}
