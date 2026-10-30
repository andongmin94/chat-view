// SPDX-License-Identifier: GPL-2.0-or-later
// Real HUD/WebView2, native controls, WinHTTP/ws and WGC. Only the provider,
// output rectangle and physical receiver consent are fixtures. No gaming-PC OBS.
#include "hud/hud-window.hpp"
#include "hud/native-chat-connection.hpp"
#include "hud/video-output-panel.hpp"
#include "hud/saved-connection.hpp"
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
    static bool retry_pending(NativeChatConnection &c) { return c.retry_first_display_pending_; }
    static void first_display_evidence(NativeChatConnection &c) {
        // Failure-only bounded phase flags: no URL, verifier, token or chat.
        const auto now = GetTickCount64();
        const auto loading_remaining = c.loading_deadline_ > now ? c.loading_deadline_ - now : 0U;
        std::cerr << "Companion first chat phase: active=" << c.active_
            << " worker=" << c.client_.running() << " awaiting_login=" << c.awaiting_login_
            << " surface_ready=" << c.surface_.ready()
            << " frames=" << c.surface_.rendered_frames() << " rows=" << c.surface_.rendered_messages()
            << " rejected=" << c.surface_.rejected_frames()
            << " metadata=" << c.connection_state_.has_value()
            << " pending_frame=" << !c.pending_.empty()
            << " awaiting_render=" << (c.awaiting_frame_ != 0U)
            << " native_ready=" << c.ready_
            << " surface_discarded=" << c.surface_.discarded()
            << " pending_subscribed=" << c.pending_subscribed_
            << " recovery_attempted=" << c.first_document_recovery_attempted_
            << " reconnecting=" << c.reconnecting_
            << " loading_remaining_ms=" << loading_remaining << '\n';
    }
};
struct NativeChatSurfaceTestAccess {
    static ICoreWebView2 *core(NativeChatSurface &s) { return s.webview_.Get(); }
};
struct VideoOutputPanelTestAccess {
    static HWND open(VideoOutputPanel &p) { p.open(); return p.panel_; }
    static void refresh(VideoOutputPanel &p) { p.refresh(); }
    static HWND output(VideoOutputPanel &p) { return p.output_; }
    static HWND cover(VideoOutputPanel &p) { return p.cover_; }
    static auto &capture(VideoOutputPanel &p) { return p.capture_; }
    static auto &check(VideoOutputPanel &p) { return p.check_; }
    static auto epoch(VideoOutputPanel &p) { return p.selection_epoch_; }
    static bool requested(VideoOutputPanel &p) { return p.requested_; }
    static bool emergency_registered(VideoOutputPanel &p) { return p.hotkey_black_; }
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
unsigned browser_requests = 0;
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
    void received(const wchar_t *marker, unsigned long long after = 0,
                  const wchar_t *excluded = nullptr, const wchar_t *also_excluded = nullptr) {
        wait([&] { return Chat::first_text(chat) && Chat::surface(chat).rendered_frames() > after &&
            Chat::surface(chat).rendered_messages() == 1; }, "fresh first text acknowledgement");
        struct Result { bool pending = false, done = false; HRESULT status = E_FAIL; std::wstring value; };
        auto result = std::make_shared<Result>();
        // Only fixed synthetic fixture markers are interpolated, never user
        // messages. Check all conditions on the same actual WebView2 DOM.
        std::wstring script = L"document.getElementById('messages').textContent.includes('" + std::wstring(marker) + L"')";
        if (excluded) script += L" && !document.getElementById('messages').textContent.includes('" +
            std::wstring(excluded) + L"')";
        if (also_excluded) script += L" && !document.getElementById('messages').textContent.includes('" +
            std::wstring(also_excluded) + L"')";
        Microsoft::WRL::ComPtr<ICoreWebView2> core = chatview::NativeChatSurfaceTestAccess::core(Chat::surface(chat));
        expect(core.Get() != nullptr, "owned chat DOM exists");
        // A heartbeat may advance rendered_frames before the newly published
        // text arrives. Wait for the actual marker, not just a frame counter;
        // keep one in-flight DOM query and the existing 15-second wait budget.
        wait([&] {
            if (result->done) {
                expect(SUCCEEDED(result->status), "owned DOM inspection succeeds");
                if (result->value == L"true") return true;
                result->pending = false; result->done = false; result->value.clear();
            }
            if (!result->pending) {
                result->pending = true;
                expect(SUCCEEDED(core->ExecuteScript(script.c_str(),
                    Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                        [result](HRESULT status, LPCWSTR value) -> HRESULT {
                            result->status = status; if (value) result->value = value; result->done = true; return S_OK;
                        }).Get())), "inspect real owned chat DOM");
            }
            return false;
        }, "current fixture text appears in the actual owned DOM");
        expect(text(Chat::dialog(chat), 113).find(L"첫 텍스트 렌더링 확인") != std::wstring::npos &&
            text(Chat::dialog(chat), 110).find(L"서버 채팅 연결: 게임 1 / 송출 0") != std::wstring::npos,
            "first text and current server socket count shown in native connection panel");
    }
};
void reapprove(Flow &flow, const std::wstring &origin, const std::wstring &other,
               const chatview::DisplayMembership &previous)
{
    auto &chat = flow.chat; auto &video = flow.video;
    const HWND dialog = Chat::dialog(chat), controls = Video::open(video);
    const HWND output = Video::output(video), cover = Video::cover(video), source = Video::source(video);
    SetDlgItemTextW(dialog, 101, origin.c_str());
    SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_CHECKED, 0);
    const auto unrelated_saved = [&] {
        auto saved = chatview::load_connection();
        const bool intact = saved && saved->credential == other && saved->origin == origin;
        if (saved) SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
        expect(intact, "fresh login never uses or deletes the unrelated saved approval");
    };
    const auto begin_login = [&] {
        const auto before = browser_requests;
        command(dialog, 104);
        flow.wait([&] { return browser_requests == before + 1; }, "one explicit fresh browser request");
        flow.state(Status::AwaitingApproval, controls);
        expect(!Chat::membership(chat) && !Chat::first_text(chat) && !Chat::surface(chat).ready() &&
            !IsWindowEnabled(GetDlgItem(dialog, 112)) && !IsWindowEnabled(GetDlgItem(dialog, 114)),
            "pending consent cannot inherit chat metadata, first text, return or management");
        expect(text(dialog, 110).find(L"새 로그인: 브라우저 승인 대기") != std::wstring::npos,
            "pending new consent is distinct from old denial or an approved connection");
        command(dialog, 104); command(dialog, 112); // Duplicate actions grant nothing.
        unrelated_saved();
    };
    // Two cancelled remembered attempts expose both saved-account substitution
    // and premature clearing of the denial fence. No provider consent is given.
    SendDlgItemMessageW(dialog, 107, BM_SETCHECK, BST_CHECKED, 0);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        if (attempt == 1) {
            command(controls, 205);
            flow.wait([&] { return !Video::capture(video).running() && pixels(RGB(0,0,0)); },
                "explicit video stop before another login attempt", 2000);
        }
        const auto epoch = Video::epoch(video), frames = Video::capture(video).snapshot().frames;
        begin_login(); flow.checkpoint("reapproval-pending");
        if (attempt == 0) flow.wait([&] {
            return Video::capture(video).snapshot().frames > frames && !IsWindowVisible(cover) && pixels(RGB(20,100,180));
        }, "fresh video continues during pending browser consent", 10000);
        else expect(!Video::requested(video) && IsWindowVisible(cover) && pixels(RGB(0,0,0)),
            "pending login cannot restart explicitly stopped video");
        command(dialog, 105); flow.state(Status::Idle, controls);
        expect(!Chat::client(chat).can_resume_current() && !Chat::membership(chat) && !Chat::first_text(chat) &&
            text(dialog, 110).find(L"이전 채팅 승인: 서버 거부") != std::wstring::npos,
            "cancelled fresh login preserves the need for new consent");
        expect(Video::epoch(video) == epoch && Video::output(video) == output && Video::cover(video) == cover,
            "login cancellation never changes video choices or output ownership");
        unrelated_saved(); flow.checkpoint("reapproval-cancelled");
    }
    // The final new approval is memory-only, preserving the other saved account.
    SendDlgItemMessageW(dialog, 107, BM_SETCHECK, BST_UNCHECKED, 0);
    begin_login(); flow.checkpoint("reapproval-approve");
    flow.wait([&] { return Chat::membership(chat).has_value() && Chat::surface(chat).rendered_frames() >= 1; },
        "new approval renders its own empty subscription");
    expect(Chat::surface(chat).rendered_messages() == 0 && !Chat::first_text(chat),
        "fresh subscription cannot inherit the revoked document's first-text acknowledgement");
    const auto renewed = Chat::membership(chat)->membership;
    expect(renewed.role == previous.role && renewed.connection_id != previous.connection_id,
        "browser consent creates a new connection with the original gaming role");
    flow.checkpoint("reapproval-empty-ready"); flow.received(L"reapproved-chat");
    flow.state(Status::Receiving, controls);
    expect(text(dialog, 110).find(L"서버 거부") == std::wstring::npos && IsWindowEnabled(GetDlgItem(dialog, 114)),
        "fresh authenticated display retires the old denial guidance");
    expect(!Video::requested(video) && !Video::check(video).active() && IsWindowVisible(cover) && pixels(RGB(0,0,0)),
        "first new chat does not restore old receiver consent or restart video");
    unrelated_saved(); flow.checkpoint("reapproved");
    command(dialog, 105); flow.state(Status::IdleResumable, controls);
    flow.checkpoint("reapproved-paused"); command(dialog, 112);
    flow.received(L"reapproved-return-chat"); flow.state(Status::Receiving, controls);
    expect(Chat::membership(chat)->membership == renewed, "manual return renews only the newly approved connection");
    expect(!Video::requested(video) && pixels(RGB(0,0,0)), "new-approval return leaves video black");
    flow.checkpoint("reapproved-returned");
    expect(!Video::accept(video), "successful reapproval is not receiver consent");
    Video::pattern(video, source); expect(Video::accept(video), "fresh receiver check explicitly restarts video");
    flow.wait([&] { return !IsWindowVisible(cover) && pixels(RGB(20,100,180)); }, "video after new explicit receiver check", 10000);
    expect(Video::output(video) == output && Video::cover(video) == cover && Video::source(video) == source &&
        Chat::membership(chat)->membership == renewed, "new approval preserves the same output windows and selected game");
    flow.checkpoint("reapproved-video-restarted"); command(dialog, 108); flow.state(Status::Idle, controls);
    expect(!Chat::membership(chat) && !Chat::first_text(chat) && !Chat::client(chat).can_resume_current(),
        "logout retires only the newly approved chat");
    expect(Video::requested(video) && !IsWindowVisible(cover) && pixels(RGB(20,100,180)),
        "new-approval logout does not implicitly stop the game video");
    unrelated_saved(); flow.checkpoint("reapproved-signed-out");
}
void run(const wchar_t *source_path, const std::wstring &origin, const std::wstring &other, std::wstring_view mode)
{
    const bool revoked = mode == L"revoked" || mode == L"reapproved";
    const bool redraw = mode == L"redraw";
    const bool reapprove_after_redraw = mode == L"redraw-reapproved-live" ||
        mode == L"redraw-reapproved-black";
    const bool revoke_during_redraw = reapprove_after_redraw ||
        mode == L"redraw-revoked-live" || mode == L"redraw-revoked-black";
    Source first(source_path), second(source_path);
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "production HUD");
    chatview::NativeChatConnection chat(hud, chatview::DisplayRole::Gaming, [](HWND, const wchar_t *) {
        ++browser_requests; std::cout << "browser-opened\n" << std::flush; return true;
    });
    chatview::VideoOutputPanel video(hud, true); Flow flow{chat, video}; hud.show_ready();
    flow.wait([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "HUD ready");
    const HWND hud_window = Chat::host(chat);
    SetWindowPos(hud_window, nullptr, 40, 40, 300, 220, SWP_NOZORDER | SWP_NOACTIVATE);
    expect(chat.open_dialog(), "gaming connection panel"); const HWND dialog = Chat::dialog(chat);
    const HWND controls = Video::open(video); expect(controls != nullptr, "video settings");
    // Both synthetic game windows have the SAME caption and executable.
    // The native picker must still distinguish their owner processes before
    // a streamer could explicitly permit video from the wrong window.
    flow.wait([&] { return source_window(first.pid) && source_window(second.pid); },
        "both equal-titled source windows ready for native selection");
    Video::refresh(video);
    const auto count = SendDlgItemMessageW(controls, 201, CB_GETCOUNT, 0, 0);
    expect(count >= 2 && count <= 256, "both real process windows are in the bounded selector");
    std::wstring first_label, second_label;
    for (LRESULT index = 0; index < count; ++index) {
        const auto length = SendDlgItemMessageW(controls, 201, CB_GETLBTEXTLEN,
            static_cast<WPARAM>(index), 0);
        expect(length >= 0 && length < 1024, "native source identity label stays bounded");
        std::wstring label(static_cast<size_t>(length) + 1, L'\0');
        expect(SendDlgItemMessageW(controls, 201, CB_GETLBTEXT,
            static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(label.data())) != CB_ERR,
            "read the actual native source selection label");
        if (label.find(L"PID " + std::to_wstring(first.pid) + L" ·") != std::wstring::npos)
            first_label = label;
        if (label.find(L"PID " + std::to_wstring(second.pid) + L" ·") != std::wstring::npos)
            second_label = label;
    }
    expect(!first_label.empty() && !second_label.empty() && first_label != second_label &&
        first_label.find(L"window-capture-test") != std::wstring::npos &&
        second_label.find(L"window-capture-test") != std::wstring::npos &&
        first_label.find(L"Synthetic game") != std::wstring::npos &&
        second_label.find(L"Synthetic game") != std::wstring::npos,
        "identical window captions resolve to distinct process-labelled choices without a second capture");
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
    if (mode == L"early-first") {
        // The synthetic provider sends the first real text during browser
        // collection, BEFORE the native lease and private WebView2 document
        // exist. A subscribed-empty frame is not a prerequisite for first text.
        try {
            flow.wait([&] { return Chat::membership(chat).has_value() &&
                Chat::first_text(chat) && Chat::surface(chat).rendered_messages() == 1U; },
                "first chat text already present at native approval");
        } catch (...) {
            Chat::first_display_evidence(chat);
            throw;
        }
        flow.received(L"initial-chat");
        flow.state(Status::Receiving, controls);
        const auto approved = Chat::membership(chat)->membership;
        expect(approved.role == chatview::DisplayRole::Gaming &&
            browser_requests == 1 && !GetModuleHandleW(L"obs.dll") &&
            !GetModuleHandleW(L"obs-frontend-api.dll"),
            "first chat from exact gaming approval uses no OBS or extra login");
        flow.checkpoint("early-first-visible");
        command(dialog, 105); // Stop local display; keep exact server approval.
        flow.wait([&] { return !Chat::client(chat).running() &&
            !Chat::membership(chat) && !Chat::surface(chat).ready() &&
            Chat::surface(chat).rendered_messages() == 0U &&
            !Chat::first_text(chat) && Chat::client(chat).can_resume_current(); },
            "local Stop clears the early text but preserves same-role return authority");
        flow.state(Status::IdleResumable, controls);
        flow.checkpoint("early-stopped");

        // A different creator may already be remembered locally. Returning
        // must prefer the exact in-run gaming approval, never that DPAPI file.
        expect(chatview::save_connection({origin, other, true}),
            "unrelated saved creator cannot replace an in-run approval");

        // The owner explicitly returns using only the approval already held
        // in memory. Neither saved credentials nor a new browser flow may
        // supply a different creator/role; the first private DOM is fresh.
        expect(IsWindowEnabled(GetDlgItem(dialog, 112)) != FALSE,
            "existing approval offers an explicit native chat return");
        command(dialog, 112);
        try {
            flow.wait([&] {
                const auto membership = Chat::membership(chat);
                return membership && membership->membership == approved &&
                    Chat::surface(chat).ready() &&
                    Chat::surface(chat).rendered_frames() >= 1U;
            }, "same gaming approval renews its native chat surface");
        } catch (...) {
            Chat::first_display_evidence(chat);
            throw;
        }
        flow.state(Status::Receiving, controls);
        expect(browser_requests == 1 && Chat::client(chat).running() &&
            !GetModuleHandleW(L"obs.dll") && !GetModuleHandleW(L"obs-frontend-api.dll"),
            "return creates one new chat connection without browser login or OBS");
        flow.checkpoint("early-returned");
        flow.received(L"returned-chat", 0U, L"initial-chat", L"bob-only-after-return");
        expect(Chat::membership(chat) &&
            Chat::membership(chat)->membership == approved,
            "new text after return is bound to the original gaming approval");
        auto saved = chatview::load_connection();
        const bool other_intact = saved && saved->credential == other &&
            saved->origin == origin && saved->developer_loopback;
        if (saved) SecureZeroMemory(saved->credential.data(),
            saved->credential.size() * sizeof(wchar_t));
        expect(other_intact, "return keeps the unrelated remembered creator unchanged");
        flow.checkpoint("early-returned-text");

        command(dialog, 105);
        flow.wait([&] { return !Chat::client(chat).running() &&
            !Chat::membership(chat) && !Chat::surface(chat).ready() &&
            Chat::surface(chat).rendered_messages() == 0U &&
            !Chat::first_text(chat) && Chat::client(chat).can_resume_current(); },
            "second local Stop clears return text without revoking the approval");
        flow.state(Status::IdleResumable, controls);
        flow.checkpoint("early-final-stopped");

        // The creator revoked this precise server-side gaming approval while
        // the local display was stopped. The button's cached eligibility
        // grants no new authority: the real refresh must be rejected.
        expect(IsWindowEnabled(GetDlgItem(dialog, 112)) != FALSE,
            "the previously renewable approval still offers a manual return attempt");
        command(dialog, 112);
        flow.wait([&] {
            return !Chat::client(chat).running() && !Chat::membership(chat) &&
                !Chat::surface(chat).ready() && !Chat::first_text(chat) &&
                !Chat::client(chat).can_resume_current() &&
                text(dialog, 110).find(L"서버 거부") != std::wstring::npos;
        }, "server revocation denies the old manual return and clears private display");
        flow.state(Status::Idle, controls);
        expect(!IsWindowEnabled(GetDlgItem(dialog, 112)) &&
            text(dialog, 113).find(L"첫 텍스트 렌더링 확인") == std::wstring::npos &&
            browser_requests == 1,
            "revocation cannot reopen the old role or log into a different account");
        flow.checkpoint("early-revoked");

        // The unrelated remembered Bob account is still present. Pressing
        // Connect must create a NEW challenge for Alice's fixed gaming role,
        // not reuse Bob or turn a failed refresh into automatic consent.
        SendDlgItemMessageW(dialog, 107, BM_SETCHECK, BST_UNCHECKED, 0);
        command(dialog, 104);
        flow.wait([&] { return browser_requests == 2U; },
            "one explicit fresh browser login after confirmed revocation");
        flow.state(Status::AwaitingApproval, controls);
        expect(!Chat::membership(chat) && !Chat::first_text(chat) &&
            !Chat::surface(chat).ready() && !IsWindowEnabled(GetDlgItem(dialog, 112)),
            "new consent pending cannot inherit a revoked sender or previous text");
        flow.checkpoint("early-reapproval-pending");

        try {
            flow.wait([&] {
                return Chat::membership(chat).has_value() &&
                    Chat::surface(chat).rendered_frames() >= 1U;
            }, "new explicit gaming approval creates a subscribed private document");
        } catch (...) {
            Chat::first_display_evidence(chat);
            throw;
        }
        expect(Chat::membership(chat)->membership.role == chatview::DisplayRole::Gaming &&
            Chat::membership(chat)->membership.connection_id != approved.connection_id &&
            Chat::surface(chat).rendered_messages() == 0U && !Chat::first_text(chat),
            "fresh owner consent receives a new gaming grant and no old first-text ACK");
        flow.state(Status::Receiving, controls);
        flow.checkpoint("early-reapproval-empty");
        flow.received(L"fresh-consent-chat", 0U, L"returned-chat", L"bob-only-after-return");
        auto saved_after_consent = chatview::load_connection();
        const bool intact = saved_after_consent && saved_after_consent->credential == other &&
            saved_after_consent->origin == origin && saved_after_consent->developer_loopback;
        if (saved_after_consent) SecureZeroMemory(saved_after_consent->credential.data(),
            saved_after_consent->credential.size() * sizeof(wchar_t));
        expect(intact && browser_requests == 2U,
            "fresh Alice consent does not replace the unrelated saved creator");
        flow.checkpoint("early-reapproved");

        command(dialog, 108); // Only the user's explicit logout revokes new approval.
        flow.wait([&] {
            return !Chat::client(chat).running() && !Chat::membership(chat) &&
                !Chat::first_text(chat) && !Chat::surface(chat).ready() &&
                !Chat::client(chat).can_resume_current();
        }, "explicit logout clears newly approved private chat and return eligibility");
        flow.state(Status::Idle, controls);
        flow.checkpoint("early-signed-out");
        expect(!GetModuleHandleW(L"obs.dll") && !GetModuleHandleW(L"obs-frontend-api.dll") &&
            !Video::requested(video) && !Video::capture(video).running(),
            "revocation, reapproval and logout never start gaming-PC OBS or independent video");
        video.close(); chat.close(); hud.destroy();
        return;
    }
    try {
        flow.wait([&] { return Chat::surface(chat).rendered_frames() >= 1 && Chat::membership(chat).has_value(); },
            "subscribed empty own DOM");
    } catch (...) {
        Chat::first_display_evidence(chat);
        throw; // Preserve the original readiness condition and 15-second bound.
    }
    expect(Chat::surface(chat).rendered_messages() == 0 && !Chat::first_text(chat), "empty subscription is not first text");
    flow.state(Status::Receiving, controls);
    // Model a retry notification already dequeued before the native owner
    // cancelled legacy page recovery. The real HUD procedure must not reload
    // its setup page over this owned document, even before the first text.
    SendMessageW(hud_window, WM_TIMER, 2U, 0);
    expect(Chat::surface(chat).ready() && Chat::membership(chat),
        "cancelled navigation retry preserves the first owned chat document");
    const auto empty_frames = Chat::surface(chat).rendered_frames();
    const auto approved = Chat::membership(chat)->membership;
    HWND game = nullptr, replacement = nullptr;
    if (revoke_during_redraw) {
        const bool playing = mode == L"redraw-revoked-live" || mode == L"redraw-reapproved-live";
        flow.wait([&] { game = source_window(first.pid); return game != nullptr; },
            "chosen source ready for video and pending first chat display");
        Video::pattern(video, game);
        const HWND output = Video::output(video), cover = Video::cover(video);
        expect(output && cover && Video::accept(video),
            "explicit receiver consent starts the independent game-window WGC output");
        flow.wait([&] { return Video::capture(video).running() && !IsWindowVisible(cover) &&
            pixels(RGB(20,100,180)); }, "fresh game pixels before the pending chat redraw", 10000);
        if (!playing) {
            command(controls, 205);
            flow.wait([&] { return !Video::capture(video).running() &&
                IsWindowVisible(cover) && pixels(RGB(0,0,0)); },
                "user-selected black output before the pending chat redraw", 2000);
            expect(!Video::accept(video), "stopped video cannot reuse its consumed receiver confirmation");
        }
        expect(chatview::save_connection({origin, other, true}),
            "another creator's remembered approval is available but may not replace the current one");
        const auto epoch = Video::epoch(video);
        const auto identifier = Video::check(video).identifier();
        const auto frames = Video::capture(video).snapshot().frames;
        expect(!Chat::first_text(chat) && Chat::surface(chat).rendered_messages() == 0 &&
            Chat::membership(chat) && Chat::membership(chat)->membership == approved &&
            text(dialog, 112) == L"현재 승인으로 채팅 표시 다시 시도",
            "only the actual subscribed no-first-text document offers a same-approval redraw");
        // The browser login and the socket metadata are present at this point;
        // the real server waits for an explicit native click, not a timer.
        flow.checkpoint("redraw-revoke-ready");
        command(dialog, 112);
        expect(Chat::retry_pending(chat) && !Chat::membership(chat) && !Chat::first_text(chat) &&
            !Chat::surface(chat).ready() && Chat::surface(chat).rendered_messages() == 0 &&
            !Chat::client(chat).login_pending(),
            "the owner clears private DOM/socket status before any authorization retry");
        expect(text(dialog, 110).find(L"서버 채팅 연결: 게임 1") == std::wstring::npos &&
            !IsWindowEnabled(GetDlgItem(dialog, 112)),
            "no stale peer count or duplicate redraw eligibility during retirement");
        command(dialog, 112); command(dialog, 104); // No second consent or replacement account.
        expect(Chat::retry_pending(chat) && !Chat::membership(chat),
            "duplicate commands cannot revive the retiring display client");
        expect(Video::output(video) == output && Video::cover(video) == cover &&
            Video::source(video) == game && Video::epoch(video) == epoch &&
            Video::check(video).identifier() == identifier && Video::requested(video) == playing,
            "explicit redraw never changes the independently selected output or its consent");
        expect(playing ? (!IsWindowVisible(cover) && pixels(RGB(20,100,180)))
                       : (IsWindowVisible(cover) && pixels(RGB(0,0,0))),
            "game-video or black-cover pixels remain unchanged before revoking chat approval");
        // A deterministic server/worker race boundary: unlike Flow::checkpoint,
        // this fixture-only read does not pump owner messages and therefore
        // cannot renew a session before the test parent revokes its exact ID.
        std::cout << "redraw-revoke-started\n" << std::flush;
        std::string acknowledge; std::getline(std::cin, acknowledge);
        expect(acknowledge == "continue", "fixture revoked the original server approval before retry");
        flow.wait([&] {
            const auto status = SendMessageW(hud_window, RegisterWindowMessageW(chatview::kQueryNativeChatMessageName), 0, 0);
            return status == static_cast<LRESULT>(Status::Idle) && !Chat::retry_pending(chat) &&
                !Chat::client(chat).running() && !Chat::client(chat).can_resume_current() &&
                !Chat::membership(chat) && !Chat::first_text(chat) &&
                !Chat::surface(chat).ready();
        }, "server rejects the retired approval during the explicit first-display redraw");
        flow.state(Status::Idle, controls);
        const auto denied = [&] {
            const auto summary = text(dialog, 110);
            expect(summary.find(L"이전 채팅 승인: 서버 거부") != std::wstring::npos &&
                summary.find(L"복귀 불가") != std::wstring::npos,
                "confirmed revocation leaves the persistent server-denied/no-return reason");
            expect(summary.find(L"확인되지 않음") != std::wstring::npos &&
                summary.find(L"서버 채팅 연결: 게임 1") == std::wstring::npos,
                "revocation clears the old server socket counts instead of reporting them as live");
            expect(summary.find(L"로그인 / 연결에서 다시 승인") != std::wstring::npos,
                "the persistent status requires a fresh explicit browser consent");
            expect(!IsWindowEnabled(GetDlgItem(dialog, 112)) &&
                text(controls, 209).find(L"복귀") == std::wstring::npos,
                "neither companion panel can return through a revoked approval");
        };
        denied();
        command(controls, 209); // Only open the already protected dialog.
        expect(IsWindowVisible(dialog) && Chat::dialog(chat) == dialog,
            "video panel reopens the same native dialog without triggering another login");
        command(dialog, 112);
        expect(!Chat::client(chat).running() && !Chat::membership(chat) &&
            browser_requests == 1, "a stale return click cannot exchange approval or open a browser");
        denied();
        auto saved = chatview::load_connection();
        const bool unrelated = saved && saved->credential == other && saved->origin == origin;
        if (saved) SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
        expect(unrelated, "revoked current approval does not read, overwrite or delete another creator's saved one");
        expect(Video::output(video) == output && Video::cover(video) == cover &&
            Video::source(video) == game && Video::epoch(video) == epoch &&
            Video::check(video).identifier() == identifier && Video::requested(video) == playing,
            "server revocation does not stop or reselect the independent WGC output");
        if (playing) {
            flow.wait([&] { return Video::capture(video).snapshot().frames > frames &&
                !IsWindowVisible(cover) && pixels(RGB(20,100,180)); },
                "game pixels and fresh WGC frames continue even after revoked chat redraw", 10000);
        } else {
            expect(!Video::capture(video).running() && IsWindowVisible(cover) &&
                pixels(RGB(0,0,0)) && !Video::accept(video),
                "a revoked chat redraw cannot uncover black output or silently reuse prior receiver consent");
        }
        expect(IsWindowVisible(hud_window) &&
            !GetModuleHandleW(L"obs.dll") && !GetModuleHandleW(L"obs-frontend-api.dll"),
            "OBS-free companion retains local HUD and never loads OBS after chat denial");
        flow.checkpoint("redraw-revoke-validated");
        if (reapprove_after_redraw) {
            // A failed explicit receiver-start check above legitimately
            // increments selection_epoch via VideoOutputPanel::stop. Snapshot
            // the current black/live state *after* that user-side action:
            // only changes during the new chat approval are forbidden.
            const auto video_epoch = Video::epoch(video);
            const auto video_identifier = Video::check(video).identifier();
            const auto video_frames = Video::capture(video).snapshot().frames;
            // Only the existing Login / Connect control can establish a new
            // role approval after server rejection. Do not use another owner's
            // saved credential, an old chat URL or a video consent token.
            SetDlgItemTextW(dialog, 101, origin.c_str());
            SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_CHECKED, 0);
            SendDlgItemMessageW(dialog, 107, BM_SETCHECK, BST_UNCHECKED, 0);
            const auto previous_browsers = browser_requests;
            command(dialog, 104);
            flow.wait([&] { return browser_requests == previous_browsers + 1U; },
                "one explicit new browser login after a confirmed revoked redraw");
            flow.state(Status::AwaitingApproval, controls);
            expect(!Chat::membership(chat) && !Chat::first_text(chat) && !Chat::surface(chat).ready() &&
                !Chat::client(chat).can_resume_current() && !IsWindowEnabled(GetDlgItem(dialog, 112)) &&
                text(dialog, 110).find(L"새 로그인: 브라우저 승인 대기") != std::wstring::npos,
                "pending new consent has no old server socket count or return authority");
            command(dialog, 104); command(dialog, 112);
            expect(browser_requests == previous_browsers + 1U &&
                !Chat::membership(chat) && !Chat::first_text(chat),
                "duplicate login/return controls do not silently authorize or substitute an account");
            expect(Video::output(video) == output && Video::cover(video) == cover &&
                Video::source(video) == game && Video::epoch(video) == video_epoch &&
                Video::check(video).identifier() == video_identifier && Video::requested(video) == playing,
                "the new browser login cannot change video target, selected output, or receiver consent");
            if (playing) flow.wait([&] {
                return Video::capture(video).snapshot().frames > video_frames &&
                    !IsWindowVisible(cover) && pixels(RGB(20,100,180));
            }, "fresh WGC game continues during new browser consent", 10000);
            else expect(!Video::capture(video).running() && !Video::requested(video) &&
                !Video::check(video).active() && IsWindowVisible(cover) && pixels(RGB(0,0,0)),
                "pending browser consent leaves video stopped with no reusable receiver check");
            flow.checkpoint("redraw-reapproval-pending");
            flow.wait([&] {
                return Chat::membership(chat) && Chat::surface(chat).rendered_frames() >= 1U &&
                    Chat::surface(chat).rendered_messages() == 0U && !Chat::first_text(chat);
            }, "newly approved gaming session renders a fresh subscribed empty DOM");
            const auto renewed = Chat::membership(chat)->membership;
            expect(renewed.role == approved.role && renewed.connection_id != approved.connection_id,
                "new browser approval has the same gaming role but a distinct server connection");
            flow.state(Status::Receiving, controls);
            // The owned DOM can acknowledge its first subscribed empty frame
            // before the native panel's status-button refresh on the next UI
            // tick. Still require BOTH the restored membership summary and
            // enabled management; do not count a stale Denied label as success.
            flow.wait([&] {
                return text(dialog, 110).find(L"이전 채팅 승인: 서버 거부") == std::wstring::npos &&
                    IsWindowEnabled(GetDlgItem(dialog, 114)) != FALSE;
            }, "confirmed new membership replaces persistent denial in the protected panel");
            expect(Video::output(video) == output && Video::cover(video) == cover &&
                Video::source(video) == game && Video::epoch(video) == video_epoch &&
                Video::check(video).identifier() == video_identifier && Video::requested(video) == playing,
                "new empty chat document leaves the video session independent");
            flow.checkpoint("redraw-reapproval-empty-ready");
            flow.received(L"redraw-new-chat");
            flow.state(Status::Receiving, controls);
            expect(Chat::membership(chat)->membership == renewed &&
                text(dialog, 113).find(L"첫 텍스트 렌더링 확인") != std::wstring::npos,
                "new first text belongs only to the new authenticated gaming approval");
            auto saved_after = chatview::load_connection();
            const bool other_preserved = saved_after && saved_after->credential == other &&
                saved_after->origin == origin;
            if (saved_after) SecureZeroMemory(saved_after->credential.data(),
                saved_after->credential.size() * sizeof(wchar_t));
            expect(other_preserved, "new memory-only browser consent preserves the other saved owner");
            expect(Video::output(video) == output && Video::cover(video) == cover &&
                Video::source(video) == game && Video::requested(video) == playing &&
                Video::epoch(video) == video_epoch,
                "first new chat text cannot restart or stop WGC game video");
            expect(playing ? (!IsWindowVisible(cover) && pixels(RGB(20,100,180)))
                           : (IsWindowVisible(cover) && pixels(RGB(0,0,0))),
                "new chat first text leaves independent game or black pixels unchanged");
            flow.checkpoint("redraw-reapproved");
            if (!playing) {
                expect(!Video::accept(video) && !Video::requested(video) &&
                    IsWindowVisible(cover) && pixels(RGB(0,0,0)),
                    "fresh chat consent alone cannot reuse an expired video receiver check");
                Video::pattern(video, game);
                expect(Video::check(video).active() && !Video::requested(video) &&
                    IsWindowVisible(cover), "only explicit new video identification creates a new check");
                expect(Video::accept(video), "new explicit receiver confirmation starts the stopped video");
                flow.wait([&] { return Video::capture(video).running() && !IsWindowVisible(cover) &&
                    pixels(RGB(20,100,180)); }, "fresh video pixels after new receiver consent", 10000);
                expect(Video::source(video) == game && Video::output(video) == output &&
                    Video::cover(video) == cover && Chat::membership(chat)->membership == renewed,
                    "explicit video restart retains the new chat approval and original game/output");
                flow.checkpoint("redraw-reapproved-video-restarted");
            }
            command(dialog, 108);
            flow.state(Status::Idle, controls);
            expect(!Chat::membership(chat) && !Chat::first_text(chat) &&
                !Chat::client(chat).can_resume_current() && Video::requested(video) &&
                !IsWindowVisible(cover) && pixels(RGB(20,100,180)),
                "explicit chat logout removes only new authority, never an independent live video");
            flow.checkpoint("redraw-reapproved-signed-out");
        }
        command(controls, 205);
        flow.wait([&] { return !Video::capture(video).running() &&
            IsWindowVisible(cover) && pixels(RGB(0,0,0)); },
            "explicit final video stop remains independent of rejected chat");
        video.close(); chat.close(); hud.destroy();
        return;
    }
    if (redraw) {
        // Do not publish a provider message yet. This deliberately reproduces
        // an authenticated, subscribed empty DOM while independent WGC video
        // is already running, then while the user keeps that output black.
        flow.wait([&] { game = source_window(first.pid); replacement = source_window(second.pid); return game && replacement; },
            "source windows ready before first text");
        Video::pattern(video, game);
        const HWND active_output = Video::output(video), active_cover = Video::cover(video);
        expect(Video::accept(video), "explicit receiver confirmation starts video before any first chat text");
        flow.wait([&] { return Video::capture(video).running() && !IsWindowVisible(active_cover) &&
            pixels(RGB(20,100,180)); }, "WGC video is playing while first chat display remains empty", 10000);
        const auto redraw_display = [&](const std::string &phase, bool playing) {
            const auto epoch = Video::epoch(video);
            const auto identifier = Video::check(video).identifier();
            const auto before_frames = Video::capture(video).snapshot().frames;
            const auto before_step = text(controls, 210), before_notice = text(controls, 207);
            expect(Chat::membership(chat) && Chat::membership(chat)->membership == approved &&
                !Chat::first_text(chat) && Chat::surface(chat).rendered_messages() == 0 &&
                text(dialog, 112) == L"현재 승인으로 채팅 표시 다시 시도" &&
                text(controls, 209) == L"채팅 표시 확인 · 연결창 열기",
                "video status links to the protected manual redraw action without automatically restarting chat");
            command(controls, 209);
            expect(IsWindowVisible(dialog) && Chat::dialog(chat) == dialog,
                "video chat opener reuses the protected connection window");
            command(dialog, 112);
            expect(Chat::retry_pending(chat) && !Chat::membership(chat) && !Chat::first_text(chat) &&
                !Chat::surface(chat).ready(), "redraw clears the old owned DOM and socket summary synchronously");
            expect(!IsWindowEnabled(GetDlgItem(dialog, 112)) &&
                !Chat::client(chat).login_pending(), "pending one-shot redraw cannot start browser consent");
            command(dialog, 112); command(dialog, 104);
            expect(Chat::retry_pending(chat) && !Chat::membership(chat),
                "duplicate redraw and Connect cannot substitute another account");
            expect(Video::output(video) == active_output && Video::cover(video) == active_cover &&
                Video::epoch(video) == epoch && Video::check(video).identifier() == identifier &&
                Video::source(video) == game && Video::requested(video) == playing &&
                text(controls, 210) == before_step && text(controls, 207) == before_notice,
                "a chat-only retry never changes video selection, consent, output, or progress");
            if (playing) expect(!IsWindowVisible(active_cover) && pixels(RGB(20,100,180)),
                "chat retry cannot black out selected live WGC output");
            else expect(IsWindowVisible(active_cover) && pixels(RGB(0,0,0)),
                "chat retry cannot uncover the explicitly stopped video output");
            flow.checkpoint(phase + "-redraw-started");
            flow.wait([&] { return !Chat::retry_pending(chat) && Chat::membership(chat) &&
                Chat::membership(chat)->membership == approved && Chat::surface(chat).rendered_frames() >= 1 &&
                Chat::surface(chat).rendered_messages() == 0 && !Chat::first_text(chat); },
                "explicit same-approval redraw creates a fresh empty owned document");
            flow.state(Status::Receiving, controls);
            expect(text(dialog, 112) == L"현재 승인으로 채팅 표시 다시 시도" &&
                Video::output(video) == active_output && Video::cover(video) == active_cover &&
                Video::source(video) == game && Video::requested(video) == playing &&
                Video::epoch(video) == epoch && Video::check(video).identifier() == identifier &&
                text(controls, 210) == before_step && text(controls, 207) == before_notice,
                "fresh empty chat cannot start/stop video or consume a pattern");
            if (playing) {
                flow.wait([&] { return Video::capture(video).snapshot().frames > before_frames &&
                    !IsWindowVisible(active_cover) && pixels(RGB(20,100,180)); },
                    "the independent WGC worker continues rendering across chat redraw", 10000);
            } else expect(!Video::capture(video).running() && IsWindowVisible(active_cover) &&
                pixels(RGB(0,0,0)), "black stop remains effective after new chat DOM is ready");
            flow.checkpoint(phase + "-redraw-ready");
        };
        redraw_display("live", true);
        command(controls, 205);
        flow.wait([&] { return !Video::capture(video).running() && IsWindowVisible(active_cover) &&
            pixels(RGB(0,0,0)); }, "explicit black stop before second early chat redraw", 2000);
        expect(!Video::accept(video), "black stop consumes the old visual receiver confirmation");
        redraw_display("black", false);
        expect(!Video::accept(video) && !Video::requested(video) &&
            IsWindowVisible(active_cover) && pixels(RGB(0,0,0)),
            "second chat redraw cannot restart black output without new receiver consent");
    }
    std::cout << "chat-empty-ready\n" << std::flush; flow.received(L"initial-chat", empty_frames);
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
    expect(Video::emergency_registered(video), "OBS-free companion registers emergency video blackout shortcut");
    // Game has focus and the companion video panel is hidden. Its HWND-bound
    // WM_HOTKEY still reaches the panel procedure, not a thread-only pump.
    ShowWindow(controls, SW_HIDE);
    const auto emergency_at = GetTickCount64();
    SendMessageW(controls, WM_HOTKEY, 0x4342, MAKELPARAM(MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'B'));
    expect(GetTickCount64() - emergency_at < 200U && !IsWindowVisible(controls) &&
           !Video::requested(video) && IsWindowVisible(cover),
        "hidden-panel emergency shortcut synchronously masks video without taking focus");
    ShowWindow(controls, SW_SHOWNOACTIVATE);
    flow.wait([&] { return !Video::capture(video).running() && IsWindowVisible(cover) && pixels(RGB(0,0,0)); },
        "emergency shortcut retains nine black output pixels", 2000);
    expect(!Video::check(video).active() && !Video::accept(video),
        "urgent stop retires visual receiver consent and cannot silently restart video");
    expect(Chat::membership(chat) && Chat::membership(chat)->membership == approved && Chat::first_text(chat),
        "emergency video blackout leaves the readable private chat and exact gaming approval live");
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
    flow.checkpoint("reselected");
    if (revoked) {
        // Keep the chosen fixture animated across the real server close/renewal.
        // Otherwise a legitimate two-second stale-frame mask would be expected.
        expect(SendMessageW(replacement, WM_APP + 42, 0, 0) != 0, "animate the existing selected fixture");
        flow.wait([&] { return !IsWindowVisible(cover) && pixels(RGB(20,100,180)); }, "animated chosen window pixels", 10000);
        expect(chatview::save_connection({origin, other, true}), "unrelated saved approval fixture");
        Microsoft::WRL::ComPtr<ICoreWebView2> core = chatview::NativeChatSurfaceTestAccess::core(Chat::surface(chat));
        expect(core.Get() != nullptr, "current owned renderer before server revocation");
        const auto epoch = Video::epoch(video), frames = Video::capture(video).snapshot().frames;
        flow.checkpoint("revoke-requested"); // Parent removes only this server membership.
        flow.state(Status::Idle, controls);
        expect(!Chat::client(chat).running() && !Chat::client(chat).can_resume_current() &&
            !Chat::membership(chat) && !Chat::first_text(chat) && !Chat::surface(chat).ready(),
            "confirmed denial retires authority, first text and socket metadata without a new login");
        const auto denied = [&] {
            const auto summary = text(dialog, 110);
            expect(summary.find(L"이전 채팅 승인: 서버 거부") != std::wstring::npos &&
                summary.find(L"복귀 불가") != std::wstring::npos && summary.find(L"확인되지 않음") != std::wstring::npos &&
                summary.find(L"서버 채팅 연결: 게임 1") == std::wstring::npos,
                "denial reason persists separately from unknown current server socket counts");
            expect(!IsWindowEnabled(GetDlgItem(dialog, 112)) && text(controls, 209).find(L"복귀") == std::wstring::npos,
                "neither panel offers a revoked approval as resumable");
        };
        denied(); roundtrip(); denied();
        struct Cleared { bool pending = false, done = false, empty = false; HRESULT status = E_FAIL; };
        auto cleared = std::make_shared<Cleared>();
        flow.wait([&] {
            if (cleared->done) {
                expect(SUCCEEDED(cleared->status), "cleared renderer inspection succeeds");
                if (cleared->empty) return true;
                cleared->pending = false; cleared->done = false;
            }
            if (!cleared->pending) {
                cleared->pending = true;
                expect(SUCCEEDED(core->ExecuteScript(L"document.querySelectorAll('#messages li').length === 0",
                    Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                        [cleared](HRESULT result, LPCWSTR value) -> HRESULT {
                            cleared->status = result;
                            cleared->empty = value && std::wstring_view(value) == L"true";
                            cleared->done = true; return S_OK;
                        }).Get())), "inspect actual cleared renderer");
            }
            return false;
        }, "server revocation clears actual previous chat rows");
        SetDlgItemTextW(dialog, 101, L"https://not-the-current-service.invalid");
        SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_UNCHECKED, 0);
        SendDlgItemMessageW(dialog, 107, BM_SETCHECK, BST_CHECKED, 0);
        command(dialog, 112); command(dialog, 105); // Even direct old commands cannot resurrect approval.
        flow.pulse(); denied();
        expect(!Chat::client(chat).running() && !Chat::membership(chat), "return does not substitute edited controls or saved account");
        auto saved = chatview::load_connection();
        const bool preserved = saved && saved->credential == other && saved->origin == origin;
        if (saved) SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
        expect(preserved, "denial neither uses nor deletes a different saved approval");
        flow.wait([&] { return Video::capture(video).snapshot().frames > frames &&
            !IsWindowVisible(cover) && pixels(RGB(20,100,180)); }, "fresh selected video continues after revocation", 10000);
        expect(Video::requested(video) && Video::epoch(video) == epoch && !Video::check(video).active() &&
            Video::output(video) == output && Video::cover(video) == cover && Video::source(video) == replacement &&
            IsWindowVisible(hud_window), "denial does not stop/reselect video or revive consumed consent");
        flow.checkpoint("revoked");
        command(controls, 205);
        flow.wait([&] { return !Video::capture(video).running() && IsWindowVisible(cover) && pixels(RGB(0,0,0)); },
            "explicit stop still masks video after chat revocation", 2000);
        expect(!Video::accept(video), "revocation is not a reusable receiver confirmation");
        Video::pattern(video, replacement);
        expect(!Video::capture(video).running() && Video::check(video).active(), "fresh identification alone cannot start capture");
        expect(Video::accept(video), "new explicit receiver confirmation restarts only game video");
        flow.wait([&] { return !IsWindowVisible(cover) && pixels(RGB(20,100,180)); }, "video restarts independently of denied chat", 10000);
        denied();
        expect(!Chat::client(chat).running() && !Chat::membership(chat), "video restart never reapproves chat");
        flow.checkpoint("revoked-video-restarted");
        if (mode == L"reapproved") reapprove(flow, origin, other, approved);
    } else {
        command(dialog, 108);
        flow.state(Status::Idle, controls);
        expect(!Chat::client(chat).can_resume_current() && !Chat::membership(chat) && !Chat::first_text(chat), "logout retires chat authority");
        expect(Video::requested(video) && !IsWindowVisible(cover) && pixels(RGB(180,70,30)), "chat logout is not an implicit video stop");
        flow.checkpoint("signed-out");
    }
    expect(!GetModuleHandleW(L"obs.dll") && !GetModuleHandleW(L"obs-frontend-api.dll"), "no OBS in gaming companion fixture");
    command(controls, 205);
    flow.wait([&] { return !Video::capture(video).running() && pixels(RGB(0,0,0)); }, "final explicit video stop", 2000);
    video.close(); chat.close(); hud.destroy();
}
}
int wmain(int argc, wchar_t **argv)
{
    try {
        expect(argc == 3, "source fixture and explicit flow mode required");
        const std::wstring_view mode(argv[2]);
        expect(mode == L"early-first" || mode == L"return" || mode == L"revoked" || mode == L"reapproved" ||
            mode == L"redraw" || mode == L"redraw-revoked-live" ||
            mode == L"redraw-revoked-black" || mode == L"redraw-reapproved-live" ||
            mode == L"redraw-reapproved-black", "known flow mode");
        std::string origin, auxiliary; std::getline(std::cin, origin); std::getline(std::cin, auxiliary);
        expect(origin.size() < 2048 && origin.starts_with("http://127.0.0.1:") && auxiliary.size() == 64, "bounded fixture input");
        std::wstring other(auxiliary.begin(), auxiliary.end());
        SecureZeroMemory(auxiliary.data(), auxiliary.size());
        struct Wipe { std::wstring &value; ~Wipe() { SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); } } wipe{other};
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        expect(GetSystemMetrics(SM_CXSCREEN) >= 900 && GetSystemMetrics(SM_CYSCREEN) >= 600, "interactive desktop required, not skipped");
        wchar_t temporary[MAX_PATH]{}, profile[MAX_PATH]{};
        expect(GetTempPathW(MAX_PATH, temporary) != 0 && GetTempFileNameW(temporary, L"CVC", 0, profile) != 0, "unique profile reservation");
        expect(DeleteFileW(profile) && CreateDirectoryW(profile, nullptr) && SetEnvironmentVariableW(L"LOCALAPPDATA", profile), "isolated profile");
        expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization");
        try { run(argv[1], std::wstring(origin.begin(), origin.end()), other, mode); } catch (...) { CoUninitialize(); throw; }
        CoUninitialize(); return 0;
    } catch (const std::exception &error) { std::cerr << "Companion chat/video: " << error.what() << '\n'; return 1; }
}
