// SPDX-License-Identifier: GPL-2.0-or-later
// Real production panel, WGC/GPU and HWNDs. Mount a synthetic output rectangle
// through test friendship: there is no production topology override. Injected
// Windows notifications are not an actual lock, hotplug or two-PC recording.
#include "hud/video-output-panel.hpp"
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
    static void mount(VideoOutputPanel &p, HWND source) {
        p.output_bounds_ = {500, 30, 820, 270};
        p.source_ = source;
        p.source_thread_ = GetWindowThreadProcessId(source, &p.source_process_);
        p.source_monitor_ = MonitorFromWindow(source, MONITOR_DEFAULTTONULL);
        p.ensure_output(); // production window + owned black cover, not copies
        p.monitor_ = MonitorFromWindow(p.output_, MONITOR_DEFAULTTONULL);
        p.requested_ = p.capture_.start(source, p.output_);
        if (!p.requested_) throw std::runtime_error("mount synthetic output");
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
    const auto near = [](int a, int b) { return a >= b - 8 && a <= b + 8; };
    return value != CLR_INVALID && near(GetRValue(value), 20) && near(GetGValue(value), 100) && near(GetBValue(value), 180);
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
void exercise(const wchar_t *source_executable)
{
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
    const struct { UINT message; WPARAM parameter; UINT recovery; WPARAM recovered; } events[] = {
        {WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK},
        {WM_POWERBROADCAST, PBT_APMSUSPEND, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC},
        {WM_DISPLAYCHANGE, 0, 0, 0},
        {WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0, 0},
    };
    for (const auto &event : events) {
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
    SendMessageW(controls, WM_COMMAND, 204, 0);
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
    std::cout << "Production video lifecycle: lock/resume, power, display/device invalidation, HWND reuse, retarget rejection, release consent and active close passed\n";
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
