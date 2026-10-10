// SPDX-License-Identifier: GPL-2.0-or-later
// Real HUD/WebView2, native controls, WinHTTP/ws and WGC. Only the provider,
// output rectangle and physical receiver consent are fixtures. No gaming-PC OBS.
#include "hud/hud-window.hpp"
#include "hud/native-chat-connection.hpp"
#include "hud/video-output-panel.hpp"
#include "common/win32-handle.hpp"
#include <wrl/event.h>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chatview {
struct NativeChatConnectionTestAccess {
    static HWND host(NativeChatConnection &c) { return c.host_; }
    static HWND dialog(NativeChatConnection &c) { return c.dialog_; }
    static auto membership(NativeChatConnection &c) { return c.connection_state_; }
    static auto &surface(NativeChatConnection &c) { return c.surface_; }
    static auto &client(NativeChatConnection &c) { return c.client_; }
    static bool first_text(NativeChatConnection &c) { return c.first_text_rendered_; }
};
struct NativeChatSurfaceTestAccess {
    static ICoreWebView2 *core(NativeChatSurface &s) { return s.webview_.Get(); }
};
struct VideoOutputPanelTestAccess {
    static HWND open(VideoOutputPanel &p) { p.open(); return p.panel_; }
    static HWND output(VideoOutputPanel &p) { return p.output_; }
    static HWND cover(VideoOutputPanel &p) { return p.cover_; }
    static auto &capture(VideoOutputPanel &p) { return p.capture_; }
    static auto &check(VideoOutputPanel &p) { return p.check_; }
    static auto epoch(VideoOutputPanel &p) { return p.selection_epoch_; }
    static bool requested(VideoOutputPanel &p) { return p.requested_; }
    static HWND source(VideoOutputPanel &p) { return p.source_; }
    static void pulse(VideoOutputPanel &p) {
        if (!p.requested_ && !p.check_.active()) { p.tick(); return; }
        // As in the existing GPU fixtures, this single-monitor rectangle is
        // NOT a permitted physical topology. Exercise real owner consumers,
        // not a production topology override or a direct cover/worker toggle.
        p.show_chat_status(); p.update_capture(p.capture_.snapshot());
    }
    static void pattern(VideoOutputPanel &p, HWND source) {
        p.output_bounds_ = {500, 30, 820, 270}; p.source_ = source;
        p.source_thread_ = GetWindowThreadProcessId(source, &p.source_process_);
        p.source_monitor_ = MonitorFromWindow(source, MONITOR_DEFAULTTONULL);
        p.ensure_output(); p.monitor_ = MonitorFromWindow(p.output_, MONITOR_DEFAULTTONULL);
        p.show_pattern(); KillTimer(p.cover_, 0x435650);
    }
    static bool accept(VideoOutputPanel &p) { p.begin_capture(); return p.requested_; }
    static bool select_source(VideoOutputPanel &p, HWND source) {
        for (size_t i = 0; i < p.sources_.size(); ++i) if (p.sources_[i].window == source) {
            if (SendDlgItemMessageW(p.panel_, 201, CB_SETCURSEL, static_cast<WPARAM>(i), 0) == CB_ERR) return false;
            SendMessageW(p.panel_, WM_COMMAND, MAKEWPARAM(201, CBN_SELCHANGE), 0); return true;
        }
        return false;
    }
};
}
namespace {
using Chat = chatview::NativeChatConnectionTestAccess;
using Video = chatview::VideoOutputPanelTestAccess;
using Status = chatview::NativeChatStatus;
void expect(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void command(HWND window, int id) { SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0); }
std::wstring text(HWND window, int id) { wchar_t value[1024]{}; GetDlgItemTextW(window, id, value, 1024); return value; }
HWND source_window(DWORD pid)
{
    struct Query { DWORD pid; HWND found = nullptr; } query{pid};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto &q = *reinterpret_cast<Query *>(data); DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner); wchar_t name[128]{};
        GetClassNameW(window, name, 128);
        if (owner == q.pid && std::wstring_view(name) == L"ChatView.SyntheticVideoSource") { q.found = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.found;
}
class Source final {
public:
    chatview::UniqueHandle process; DWORD pid = 0;
    explicit Source(const wchar_t *path) {
        std::wstring args = L"\"" + std::wstring(path) + L"\" --source";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION info{};
        expect(CreateProcessW(path, args.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info) != FALSE,
            "start synthetic game window");
        process.reset(info.hProcess); CloseHandle(info.hThread); pid = info.dwProcessId;
    }
    ~Source() { if (WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT) {
        TerminateProcess(process.get(), 99); WaitForSingleObject(process.get(), 2000);
    } }
};
bool pixels(COLORREF expected)
{
    const HDC dc = GetDC(nullptr); if (!dc) return false;
    bool match = true;
    const auto close = [](int a, int b) { return a >= b - 8 && a <= b + 8; };
    for (int x : {80, 160, 240}) for (int y : {60, 120, 180}) {
        const auto actual = GetPixel(dc, 500 + x, 30 + y);
        match = match && actual != CLR_INVALID && close(GetRValue(actual), GetRValue(expected)) &&
            close(GetGValue(actual), GetGValue(expected)) && close(GetBValue(actual), GetBValue(expected));
    }
    ReleaseDC(nullptr, dc); return match;
}
struct Flow {
    chatview::NativeChatConnection &chat; chatview::VideoOutputPanel &video;
    void pulse() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            expect(message.message != WM_QUIT, "HUD remains alive");
            if (video.dispatch(message) || chat.dispatch(message)) continue;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        chat.tick(); Video::pulse(video);
    }
    void wait(const std::function<bool()> &done, const char *message, ULONGLONG ms = 15000) {
        const auto due = GetTickCount64() + ms;
        while (!done()) { expect(GetTickCount64() < due, message); pulse(); Sleep(10); }
    }
    void checkpoint(const std::string &phase) {
        std::cout << phase << '\n' << std::flush;
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE); std::string reply;
        wait([&] {
            DWORD available = 0;
            expect(PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr) != FALSE, "fixture reply pipe");
            while (available--) {
                char c = 0; DWORD read = 0;
                expect(ReadFile(input, &c, 1, &read, nullptr) && read == 1, "fixture reply byte");
                if (c == '\n') { expect(reply == "continue", "fixture checkpoint acknowledged"); return true; }
                if (c != '\r') reply += c;
                expect(reply.size() < 32, "bounded fixture reply");
            }
            return false;
        }, "server observes the actual socket/approval transition", 5000);
    }
    void state(Status expected, HWND controls) {
        const UINT query = RegisterWindowMessageW(chatview::kQueryNativeChatMessageName);
        expect(query != 0, "local status query");
        wait([&] {
            return SendMessageW(Chat::host(chat), query, 0, 0) == static_cast<LRESULT>(expected) &&
                text(controls, 212) == chatview::native_chat_status_text_ko(expected);
        }, "video panel independently follows the actual native chat state");
    }
    void received(const wchar_t *marker, unsigned long long after = 0) {
        wait([&] { return Chat::first_text(chat) && Chat::surface(chat).rendered_frames() > after &&
            Chat::surface(chat).rendered_messages() == 1; }, "fresh first text acknowledgement");
        struct Result { bool done = false; HRESULT status = E_FAIL; std::wstring value; };
        auto result = std::make_shared<Result>();
        const std::wstring script = L"document.getElementById('messages').textContent.includes('" + std::wstring(marker) + L"')";
        auto *core = chatview::NativeChatSurfaceTestAccess::core(Chat::surface(chat));
        expect(core && SUCCEEDED(core->ExecuteScript(script.c_str(),
            Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [result](HRESULT status, LPCWSTR value) -> HRESULT {
                    result->status = status; if (value) result->value = value; result->done = true; return S_OK;
                }).Get())), "inspect real owned chat DOM");
        wait([&] { return result->done; }, "owned DOM inspection completes");
        expect(SUCCEEDED(result->status) && result->value == L"true", "current fixture text, not old DOM content");
        expect(text(Chat::dialog(chat), 113).find(L"첫 텍스트 렌더링 확인") != std::wstring::npos &&
            text(Chat::dialog(chat), 110).find(L"서버 채팅 연결: 게임 1 / 송출 0") != std::wstring::npos,
            "first text and current server socket count shown in native connection panel");
    }
};
void run(const wchar_t *source_path, const std::wstring &origin)
{
    Source first(source_path), second(source_path);
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "production HUD");
    chatview::NativeChatConnection chat(hud, chatview::DisplayRole::Gaming, [](HWND, const wchar_t *) {
        std::cout << "browser-opened\n" << std::flush; return true;
    });
    chatview::VideoOutputPanel video(hud, true); Flow flow{chat, video}; hud.show_ready();
    flow.wait([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "HUD ready");
    const HWND hud_window = Chat::host(chat);
    SetWindowPos(hud_window, nullptr, 40, 40, 300, 220, SWP_NOZORDER | SWP_NOACTIVATE);
    expect(chat.open_dialog(), "gaming connection panel"); const HWND dialog = Chat::dialog(chat);
    const HWND controls = Video::open(video); expect(controls != nullptr, "video settings");
    SetWindowPos(dialog, nullptr, 20, 350, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(controls, nullptr, 20, 350, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    expect(video.wait_timeout() == 100, "visible unselected panel refreshes chat even without capture");
    ShowWindow(controls, SW_HIDE); expect(video.wait_timeout() == INFINITE, "hidden idle panel does not poll");
    expect(Video::open(video) == controls, "same settings window reopens"); flow.state(Status::Idle, controls);
    RECT bounds{}, status{}; expect(GetClientRect(controls, &bounds) && GetWindowRect(GetDlgItem(controls, 212), &status), "status geometry");
    MapWindowPoints(nullptr, controls, reinterpret_cast<POINT *>(&status), 2);
    expect(status.left >= 0 && status.top >= 0 && status.right <= bounds.right && status.bottom <= bounds.bottom,
        "new chat status fits inside the existing panel");
    SetDlgItemTextW(dialog, 101, origin.c_str()); SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_CHECKED, 0);
    command(dialog, 104);
    flow.wait([&] { return Chat::surface(chat).rendered_frames() >= 1 && Chat::membership(chat).has_value(); }, "subscribed empty own DOM");
    expect(Chat::surface(chat).rendered_messages() == 0 && !Chat::first_text(chat), "empty subscription is not first text");
    flow.state(Status::Receiving, controls);
    const auto empty_frames = Chat::surface(chat).rendered_frames();
    std::cout << "chat-empty-ready\n" << std::flush; flow.received(L"initial-chat", empty_frames);
    const auto approved = Chat::membership(chat)->membership;
    HWND game = nullptr, replacement = nullptr;
    flow.wait([&] { game = source_window(first.pid); replacement = source_window(second.pid); return game && replacement; }, "two source fixtures ready");
    expect(SendMessageW(replacement, WM_APP + 43, MAKEWPARAM(400, 300), RGB(180,70,30)) != 0, "distinct replacement pixels");
    Video::pattern(video, game); const HWND output = Video::output(video), cover = Video::cover(video);
    expect(Video::check(video).active() && !Video::requested(video), "identification is not capture");
    const auto roundtrip = [&] {
        const auto epoch = Video::epoch(video), identifier = static_cast<std::uint64_t>(Video::check(video).identifier());
        const auto notice = text(controls, 207), step = text(controls, 210);
        SendMessageW(dialog, WM_CLOSE, 0, 0); command(controls, 209);
        expect(Chat::dialog(chat) == dialog && IsWindowVisible(dialog), "video opens same protected chat panel");
        ShowWindow(controls, SW_HIDE); command(dialog, 116);
        flow.wait([&] { return IsWindowVisible(controls) != FALSE; }, "chat returns to existing video panel");
        expect(Video::open(video) == controls && Video::output(video) == output && Video::cover(video) == cover &&
            Video::epoch(video) == epoch && Video::check(video).identifier() == identifier &&
            text(controls, 207) == notice && text(controls, 210) == step, "panel navigation preserves video state and consent");
    };
    const auto pause_return = [&](const std::string &phase, const wchar_t *marker) {
        const auto epoch = Video::epoch(video), identifier = static_cast<std::uint64_t>(Video::check(video).identifier());
        const auto frames = Video::capture(video).snapshot().frames;
        const bool requested = Video::requested(video);
        command(dialog, 105);
        expect(!Chat::membership(chat) && !Chat::first_text(chat) && !Chat::surface(chat).ready(),
            "chat stop synchronously clears socket metadata, text and document");
        flow.state(Status::IdleResumable, controls);
        expect(text(dialog, 110).find(L"확인되지 않음") != std::wstring::npos &&
            text(controls, 209).find(L"복귀") != std::wstring::npos, "stopped chat offers explicit return without old socket counts");
        roundtrip(); flow.checkpoint(phase + "-paused");
        expect(Video::requested(video) == requested && Video::epoch(video) == epoch &&
            Video::check(video).identifier() == identifier, "chat stop cannot start/stop capture or change pattern consent");
        command(dialog, 112);
        expect(!Chat::membership(chat) && !Chat::first_text(chat), "return needs new metadata and text acknowledgements");
        flow.received(marker); flow.state(Status::Receiving, controls);
        expect(Chat::membership(chat)->membership == approved, "return keeps the same approved role/session/connection");
        flow.checkpoint(phase + "-returned");
        expect(Video::requested(video) == requested && Video::epoch(video) == epoch &&
            Video::check(video).identifier() == identifier && Video::capture(video).snapshot().frames >= frames,
            "chat return cannot restart the video worker or revive consumed consent");
    };
    pause_return("pattern", L"pattern-return");
    expect(Video::accept(video), "explicit receiver fixture confirmation starts WGC");
    flow.wait([&] { return !IsWindowVisible(cover) && pixels(RGB(20,100,180)); }, "initial WGC video pixels", 10000);
    pause_return("capture", L"capture-return");
    expect(!IsWindowVisible(cover) && pixels(RGB(20,100,180)), "chat stop/return leaves selected video playing");
    command(controls, 205);
    flow.wait([&] { return !Video::capture(video).running() && IsWindowVisible(cover) && pixels(RGB(0,0,0)); }, "video stop retains black output", 2000);
    expect(Chat::membership(chat) && Chat::membership(chat)->membership == approved && Chat::first_text(chat),
        "video stop leaves chat receiving on the same approval");
    const auto before = Chat::surface(chat).rendered_frames(); flow.checkpoint("video-stopped");
    flow.received(L"video-stopped-chat-live", before);
    command(controls, 203); // Real target enumeration, no automatic selection.
    expect(SendDlgItemMessageW(controls, 201, CB_GETCURSEL, 0, 0) == CB_ERR, "refresh requires explicit new game choice");
    expect(Video::select_source(video, replacement), "choose a different enumerated source");
    const auto selection = SendDlgItemMessageW(controls, 201, CB_GETCURSEL, 0, 0);
    pause_return("black", L"black-return");
    expect(!Video::requested(video) && !Video::check(video).active() && pixels(RGB(0,0,0)) &&
        SendDlgItemMessageW(controls, 201, CB_GETCURSEL, 0, 0) == selection,
        "chat return preserves black output and new selection without video consent");
    expect(!Video::accept(video), "old consumed video confirmation cannot be reused");
    Video::pattern(video, replacement); expect(Video::accept(video), "new source requires a new pattern confirmation");
    flow.wait([&] { return !IsWindowVisible(cover) && pixels(RGB(180,70,30)); }, "new source displayed through same WGC output", 10000);
    expect(Video::output(video) == output && Video::cover(video) == cover && Video::source(video) == replacement &&
        Chat::membership(chat)->membership == approved && IsWindowVisible(hud_window), "reselection preserves output windows and readable chat");
    flow.checkpoint("reselected"); command(dialog, 108);
    flow.state(Status::Idle, controls);
    expect(!Chat::client(chat).can_resume_current() && !Chat::membership(chat) && !Chat::first_text(chat), "logout retires chat authority");
    expect(Video::requested(video) && !IsWindowVisible(cover) && pixels(RGB(180,70,30)), "chat logout is not an implicit video stop");
    flow.checkpoint("signed-out");
    expect(!GetModuleHandleW(L"obs.dll") && !GetModuleHandleW(L"obs-frontend-api.dll"), "no OBS in gaming companion fixture");
    command(controls, 205);
    flow.wait([&] { return !Video::capture(video).running() && pixels(RGB(0,0,0)); }, "final explicit video stop", 2000);
    video.close(); chat.close(); hud.destroy();
}
}
int wmain(int argc, wchar_t **argv)
{
    try {
        expect(argc == 2, "source fixture executable required"); std::string origin; std::getline(std::cin, origin);
        expect(origin.starts_with("http://127.0.0.1:"), "local fixture origin");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        expect(GetSystemMetrics(SM_CXSCREEN) >= 900 && GetSystemMetrics(SM_CYSCREEN) >= 600, "interactive desktop required, not skipped");
        wchar_t temporary[MAX_PATH]{}, profile[MAX_PATH]{};
        expect(GetTempPathW(MAX_PATH, temporary) != 0 && GetTempFileNameW(temporary, L"CVC", 0, profile) != 0, "unique profile reservation");
        expect(DeleteFileW(profile) && CreateDirectoryW(profile, nullptr) && SetEnvironmentVariableW(L"LOCALAPPDATA", profile), "isolated profile");
        expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization");
        try { run(argv[1], std::wstring(origin.begin(), origin.end())); } catch (...) { CoUninitialize(); throw; }
        CoUninitialize(); return 0;
    } catch (const std::exception &error) { std::cerr << "Companion chat/video: " << error.what() << '\n'; return 1; }
}
