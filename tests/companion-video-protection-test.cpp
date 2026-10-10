// SPDX-License-Identifier: GPL-2.0-or-later
// Production HUD, native chat panel, companion video panel and WGC/GPU.
// Only output topology/receiver consent and protection-loss timing are synthetic.
// No live provider, physical HDMI, OBS installation or paid-exposure assertion.
#include "hud/video-output-panel.hpp"
#include "hud/native-chat-connection.hpp"
#include "hud/hud-window.hpp"
#include "hud/video-frame-time.hpp"
#include "common/win32-handle.hpp"
#include <commctrl.h>
#include <filesystem>
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
    static HWND source(VideoOutputPanel &p) { return p.source_; }
    static bool requested(VideoOutputPanel &p) { return p.requested_; }
    static auto &capture(VideoOutputPanel &p) { return p.capture_; }
    static auto &check(VideoOutputPanel &p) { return p.check_; }
    static bool choices_empty(VideoOutputPanel &p) { return p.sources_.empty() && p.monitors_.empty(); }
    static void apply(VideoOutputPanel &p, const WindowCaptureSnapshot &s) { p.update_capture(s); }
    static void pattern(VideoOutputPanel &p, HWND game) {
        p.output_bounds_ = {500, 30, 820, 270};
        p.source_ = game;
        p.source_thread_ = GetWindowThreadProcessId(game, &p.source_process_);
        p.source_monitor_ = MonitorFromWindow(game, MONITOR_DEFAULTTONULL);
        p.ensure_output();
        p.monitor_ = MonitorFromWindow(p.output_, MONITOR_DEFAULTTONULL);
        p.show_pattern();
        // Like video-output-lifecycle: the rectangle is not a real extended
        // monitor. Explicit loss and queued timer checks still use production.
        KillTimer(p.cover_, 0x435650);
    }
    static bool accept(VideoOutputPanel &p) { p.begin_capture(); return p.requested_; }
};
struct NativeChatConnectionTestAccess {
    static HWND window(NativeChatConnection &c) { return c.dialog_; }
    static bool idle(NativeChatConnection &c) {
        return !c.active_ && !c.client_.running() && !c.client_.can_resume_current() && !c.signing_out_;
    }
};
}
namespace {
using Video = chatview::VideoOutputPanelTestAccess;
using Chat = chatview::NativeChatConnectionTestAccess;
void expect(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
void pump()
{
    MSG m{};
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        expect(m.message != WM_QUIT, "no unexpected process exit");
        TranslateMessage(&m); DispatchMessageW(&m);
    }
}
void await(const std::function<bool()> &condition, const char *why, ULONGLONG limit = 5000)
{
    const auto until = GetTickCount64() + limit;
    while (!condition()) { expect(GetTickCount64() < until, why); pump(); Sleep(10); }
}
HWND find(DWORD pid, const wchar_t *klass, bool title = false)
{
    struct Query { DWORD pid; const wchar_t *klass; bool title; HWND window = nullptr; } q{pid, klass, title};
    EnumWindows([](HWND w, LPARAM data) -> BOOL {
        auto &query = *reinterpret_cast<Query *>(data);
        DWORD owner = 0; GetWindowThreadProcessId(w, &owner);
        wchar_t name[128]{};
        if (query.title) GetWindowTextW(w, name, 128); else GetClassNameW(w, name, 128);
        if (owner == query.pid && std::wstring_view(name) == query.klass) { query.window = w; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&q));
    return q.window;
}
class Source final {
public:
    chatview::UniqueHandle process;
    DWORD pid = 0;
    explicit Source(const wchar_t *path) {
        std::wstring command = L"\"" + std::wstring(path) + L"\" --source";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        expect(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
            &startup, &info) != FALSE, "launch existing synthetic window source");
        process.reset(info.hProcess); CloseHandle(info.hThread); pid = info.dwProcessId;
    }
    ~Source() {
        if (WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.get(), 99); WaitForSingleObject(process.get(), 2000);
        }
    }
};
std::wstring text(HWND owner, int id)
{
    wchar_t value[1024]{};
    expect(GetDlgItemTextW(owner, id, value, 1024) > 0, "native status control contains text");
    return value;
}
void affinity(HWND window, DWORD value)
{
    DWORD observed = 0;
    expect(SetWindowDisplayAffinity(window, value) && GetWindowDisplayAffinity(window, &observed) &&
        observed == value, "actual window affinity change and readback");
}
bool pixels(COLORREF expected)
{
    const HDC dc = GetDC(nullptr); if (!dc) return false;
    bool match = true;
    for (int x : {1, 2, 3}) for (int y : {1, 2, 3}) {
        const auto actual = GetPixel(dc, 500 + x * 80, 30 + y * 60);
        const auto near_value = [](int a, int b) { return a >= b - 8 && a <= b + 8; };
        match = actual != CLR_INVALID && near_value(GetRValue(actual), GetRValue(expected)) &&
            near_value(GetGValue(actual), GetGValue(expected)) && near_value(GetBValue(actual), GetBValue(expected)) && match;
    }
    ReleaseDC(nullptr, dc); return match;
}
MSG request_video(HWND chat, bool allowed)
{
    const UINT message = RegisterWindowMessageW(chatview::kOpenCompanionVideoMessageName);
    expect(message != 0, "registered existing companion message");
    MSG queued{};
    expect(!PeekMessageW(&queued, nullptr, message, message, PM_REMOVE), "no stale video request before click");
    SendMessageW(chat, WM_COMMAND, 116, 0);
    const bool posted = PeekMessageW(&queued, nullptr, message, message, PM_REMOVE) != FALSE;
    expect(posted == allowed, "only a protected visible gaming panel can post video request");
    if (posted) expect(!queued.hwnd && !queued.wParam && !queued.lParam, "local request has no authority payload");
    return queued;
}
void return_to_video(chatview::VideoOutputPanel &p, HWND chat, HWND controls)
{
    ShowWindow(controls, SW_HIDE); // Window visibility only, not the explicit Stop/Close command.
    auto request = request_video(chat, true);
    expect(p.dispatch(request) && Video::open(p) == controls && IsWindowVisible(controls),
        "protected native request reopens the same companion panel");
}
void start_pixels(chatview::VideoOutputPanel &p, HWND game)
{
    Video::pattern(p, game);
    expect(Video::check(p).active() && !Video::capture(p).running(), "new pattern is not game capture");
    expect(Video::accept(p) && IsWindowVisible(Video::cover(p)), "new consent starts beneath the black cover");
    await([&] {
        const auto sample = Video::capture(p).snapshot(); Video::apply(p, sample);
        return Video::requested(p) && sample.frames >= 2 && sample.width == 400 && sample.height == 300 &&
            chatview::video_frame_fresh(GetTickCount64(), sample.content_at_ms) &&
            !IsWindowVisible(Video::cover(p)) && pixels(RGB(20,100,180));
    }, "fresh production WGC pixels after explicit new consent", 10000);
}
void black_stopped(chatview::VideoOutputPanel &p, HWND output, HWND cover)
{
    expect(!Video::requested(p) && !Video::check(p).active() && Video::source(p) == nullptr &&
        Video::choices_empty(p) && Video::output(p) == output && Video::cover(p) == cover && IsWindowVisible(cover),
        "loss retires selection/consent synchronously but preserves the independent black HWNDs");
    await([&] { return !Video::capture(p).running() && pixels(RGB(0,0,0)); },
        "WGC stops beneath nine black output samples", 2000);
}
class LoseDuringPaint final {
public:
    HWND cover, hud;
    bool fired = false, changed = false;
    LoseDuringPaint(HWND c, HWND h) : cover(c), hud(h) {
        expect(SetWindowSubclass(cover, procedure, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE,
            "install scoped synchronous protection-loss fixture");
    }
    ~LoseDuringPaint() { RemoveWindowSubclass(cover, procedure, 1); }
private:
    static LRESULT CALLBACK procedure(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto &self = *reinterpret_cast<LoseDuringPaint *>(data);
        if (m == WM_PAINT && !self.fired) {
            self.fired = true; // No nested command/tick: test the post-mask recheck itself.
            self.changed = SetWindowDisplayAffinity(self.hud, WDA_NONE) != FALSE;
        }
        return DefSubclassProc(w, m, wp, lp);
    }
};
void exercise(const wchar_t *source_path)
{
    expect(GetSystemMetrics(SM_CXSCREEN) >= 900 && GetSystemMetrics(SM_CYSCREEN) >= 600,
        "interactive desktop required, not skipped");
    Source source(source_path); HWND game = nullptr;
    await([&] { game = find(source.pid, L"ChatView.SyntheticVideoSource"); return game && IsWindowVisible(game); }, "game source ready");
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "create actual HUD"); hud.show_ready();
    await([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "actual WebView2 HUD ready", 20000);
    // Resolve the production HWND by title rather than assuming its class name.
    const HWND hud_window = find(GetCurrentProcessId(), L"ChatView HUD", true);
    DWORD hud_pid = 0; GetWindowThreadProcessId(hud_window, &hud_pid);
    expect(hud_window && hud_pid == GetCurrentProcessId(), "own HUD window");
    SetWindowPos(hud_window, nullptr, 40, 40, 300, 220, SWP_NOZORDER | SWP_NOACTIVATE);
    chatview::NativeChatConnection chat(hud, chatview::DisplayRole::Gaming);
    chat.tick(); // Empty isolated profile; never starts a provider or a browser.
    expect(chat.open_dialog(), "actual protected gaming chat dialog opens");
    const HWND dialog = Chat::window(chat);
    SetWindowPos(dialog, nullptr, 20, 350, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    chatview::VideoOutputPanel video(hud, true);
    const HWND controls = Video::open(video);
    expect(controls && GetDlgItem(dialog, 116) && text(dialog, 109).find(L"게임") != std::wstring::npos,
        "gaming role and its existing video button");
    SetWindowPos(controls, nullptr, 20, 350, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    DWORD protected_panel = 0;
    expect(GetWindowDisplayAffinity(controls, &protected_panel) && protected_panel == WDA_EXCLUDEFROMCAPTURE,
        "video settings has readable actual exclusion before first use");
    expect(Chat::idle(chat) && !Video::output(video), "panel navigation alone creates no login or capture");

    Video::pattern(video, game);
    const HWND output = Video::output(video), cover = Video::cover(video);
    const auto identifier = Video::check(video).identifier();
    const auto pattern_notice = text(controls, 207), pattern_step = text(controls, 210);
    affinity(dialog, WDA_NONE);
    request_video(dialog, false);
    expect(!IsWindowVisible(dialog), "unprotected originating chat panel is hidden rather than repaired");
    SendMessageW(controls, WM_COMMAND, 209, 0);
    expect(!IsWindowVisible(dialog) && text(controls, 211).find(L"열기 실패") != std::wstring::npos &&
        text(controls, 207) == pattern_notice && text(controls, 210) == pattern_step &&
        Video::check(video).identifier() == identifier && Video::check(video).active() && !Video::capture(video).running(),
        "chat-only protection failure cannot overwrite the pattern label or consume consent");
    affinity(dialog, WDA_EXCLUDEFROMCAPTURE);
    SendMessageW(controls, WM_COMMAND, 209, 0);
    expect(IsWindowVisible(dialog) && Chat::window(chat) == dialog, "explicit return reuses the protected chat HWND");
    return_to_video(video, dialog, controls);
    expect(text(controls, 207) == pattern_notice && Video::check(video).identifier() == identifier,
        "healthy panel round trip preserves the same unconsumed pattern");
    SendMessageW(controls, WM_COMMAND, 205, 0);
    start_pixels(video, game);
    const auto capture_notice = text(controls, 207);
    affinity(dialog, WDA_NONE); request_video(dialog, false);
    SendMessageW(controls, WM_COMMAND, 209, 0);
    expect(Video::requested(video) && Video::capture(video).running() && !IsWindowVisible(cover) &&
        Video::source(video) == game && text(controls, 207) == capture_notice && pixels(RGB(20,100,180)),
        "chat-only refusal leaves the independently authorized live video unchanged");
    affinity(dialog, WDA_EXCLUDEFROMCAPTURE);
    SendMessageW(controls, WM_COMMAND, 209, 0);
    return_to_video(video, dialog, controls);
    expect(Video::requested(video) && Video::output(video) == output && !IsWindowVisible(cover),
        "healthy active round trip never restarts capture or replaces output");

    auto late = Video::capture(video).snapshot();
    affinity(hud_window, WDA_NONE);
    Video::apply(video, late); // A still-fresh snapshot cannot outrank real protection loss.
    expect(!Video::requested(video) && !IsWindowVisible(controls) &&
        text(controls, 207).find(L"보호를 확인하지 못해") != std::wstring::npos,
        "fresh-snapshot boundary denies output and records the protection reason");
    affinity(hud_window, WDA_EXCLUDEFROMCAPTURE); // Synthetic restoration is not automatic production repair.
    black_stopped(video, output, cover);
    late.content_at_ms = GetTickCount64(); Video::apply(video, late);
    SendMessageW(cover, WM_TIMER, 0x435650, 0);
    return_to_video(video, dialog, controls);
    expect(!Video::requested(video) && !Video::check(video).active() && pixels(RGB(0,0,0)) &&
        text(controls, 210).find(L"이전 대상 선택 무효") != std::wstring::npos,
        "restored protection, queued timer, late frame and panel return cannot revive prior consent");
    expect(!Video::accept(video), "old receiver confirmation is not reusable");

    Video::pattern(video, game);
    auto queued = request_video(dialog, true);
    affinity(controls, WDA_NONE);
    expect(video.dispatch(queued) && !IsWindowVisible(controls),
        "protection is checked when queued return executes, not just when posted");
    expect(GetWindowDisplayAffinity(controls, &protected_panel) && protected_panel == WDA_NONE,
        "denied request does not silently restore settings affinity");
    affinity(controls, WDA_EXCLUDEFROMCAPTURE);
    black_stopped(video, output, cover);
    return_to_video(video, dialog, controls);
    expect(!Video::accept(video), "panel protection loss also retires the receiver check");

    for (bool capture_transition : {false, true}) {
        Video::pattern(video, game);
        const auto before = Video::capture(video).snapshot().frames;
        {
            LoseDuringPaint loss(cover, hud_window);
            if (capture_transition) expect(!Video::accept(video), "mask-time loss blocks worker start");
            else Video::pattern(video, game);
            expect(loss.fired && loss.changed, "actual synchronous paint injected protection loss");
        }
        affinity(hud_window, WDA_EXCLUDEFROMCAPTURE);
        black_stopped(video, output, cover);
        expect(Video::capture(video).snapshot().frames == before,
            "neither pattern nor capture transition starts a worker after mask-time loss");
        return_to_video(video, dialog, controls);
    }
    start_pixels(video, game);
    expect(Chat::idle(chat) && Chat::window(chat) == dialog && Video::output(video) == output &&
        Video::cover(video) == cover && IsWindowVisible(hud_window) &&
        !GetModuleHandleW(L"obs.dll") && !GetModuleHandleW(L"libobs.dll"),
        "new explicit consent restores actual video without OBS, another login or replacement windows");
    SendMessageW(controls, WM_COMMAND, 205, 0);
    await([&] { return !Video::capture(video).running() && pixels(RGB(0,0,0)); }, "final explicit black stop", 2000);
    video.close(); chat.close(); hud.destroy();
    std::cout << "Companion protection: actual affinity, independent requests, black pixels, single-use consent and fresh WGC restart passed\n";
}
}
int wmain(int argc, wchar_t **argv)
{
    std::filesystem::path profile;
    try {
        expect(argc == 2, "synthetic-source executable required");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        wchar_t temporary[MAX_PATH]{}, unique[MAX_PATH]{};
        const auto length = GetTempPathW(MAX_PATH, temporary);
        expect(length && length < MAX_PATH && GetTempFileNameW(temporary, L"cvp", 0, unique), "reserve unique profile path");
        profile = unique;
        expect(DeleteFileW(unique) && CreateDirectoryW(unique, nullptr) &&
            SetEnvironmentVariableW(L"LOCALAPPDATA", unique), "isolated native profile");
        expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization");
        try { exercise(argv[1]); } catch (...) { CoUninitialize(); throw; }
        CoUninitialize();
        std::error_code ignored; std::filesystem::remove_all(profile, ignored);
        return 0;
    } catch (const std::exception &error) {
        if (!profile.empty()) { std::error_code ignored; std::filesystem::remove_all(profile, ignored); }
        std::cerr << "Companion video protection: " << error.what() << '\n'; return 1;
    }
}
