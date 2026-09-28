// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic frontend observation -> actual shared mapping/reader -> WinHTTP ->
// real service -> production HUD controls. Not a live OBS or capture test.
#include "hud/display-client.hpp"
#include "hud/native-chat-connection.hpp"
#include "hud/hud-window.hpp"
#include "hud/shared-state-reader.hpp"
#include "common/win32-handle.hpp"
#include <Windows.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace chatview {
struct NativeChatConnectionTestAccess {
    static void begin(NativeChatConnection &c, const std::wstring &origin, const std::wstring &token)
    { c.begin(origin, token, true, DisplayAuthentication::Saved); }
    static void disable_auto(NativeChatConnection &c) { c.auto_connect_pending_ = false; }
    static HWND dialog(NativeChatConnection &c) { c.open_dialog(); return c.dialog_; }
    static const std::optional<DisplayConnectionState> &state(NativeChatConnection &c) { return c.connection_state_; }
    static NativeChatSurface &surface(NativeChatConnection &c) { return c.surface_; }
};
}
namespace {
void expect(bool value, const char *label) { if (!value) throw std::runtime_error(label); }
std::wstring read_input()
{
    std::string line; std::getline(std::cin, line);
    expect(!line.empty() && line.size() <= 2048, "bounded fixture input");
    return {line.begin(), line.end()};
}
void command(const char *value)
{
    std::cout << value << '\n' << std::flush;
    std::string ack; std::getline(std::cin, ack); expect(ack == value, "fixture stage acknowledgement");
}
std::wstring status_text(HWND dialog)
{
    wchar_t text[1024]{}; GetDlgItemTextW(dialog, 110, text, 1024); return text;
}
struct Mapping {
    chatview::UniqueHandle mapping, event;
    chatview::SharedState *state = nullptr;
    std::wstring name, event_name;
    Mapping()
    {
        name = L"Local\\ChatView.OutputTest." + std::to_wstring(GetCurrentProcessId());
        event_name = name + L".event";
        mapping.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(sizeof(chatview::SharedState)), name.c_str()));
        event.reset(CreateEventW(nullptr, FALSE, FALSE, event_name.c_str()));
        expect(mapping && event, "create local output transport");
        state = static_cast<chatview::SharedState *>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0,
            sizeof(chatview::SharedState)));
        expect(state != nullptr, "map local output transport");
        *state = {};
        state->magic = chatview::kSharedStateMagic; state->version = chatview::kSharedStateVersion;
    }
    ~Mapping() { if (state) UnmapViewOfFile(state); }
};
void exercise()
{
    const auto origin = read_input(), gaming_token = read_input(), streaming_token = read_input();
    expect(gaming_token.size() == 64 && streaming_token.size() == 64, "opaque fixture credentials");
    wchar_t temporary[MAX_PATH]{}; expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary profile");
    const auto profile = std::wstring(temporary) + L"ChatView-Output-" + std::to_wstring(GetCurrentProcessId());
    expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE, "isolated profile");
    expect(SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str()) != FALSE, "private test profile");
    Mapping transport;
    chatview::SharedStateReader reader;
    expect(reader.open(transport.name, transport.event_name, GetCurrentProcessId()), "open production IPC reader");
    expect(reader.read_outputs().sampled_tick == 0, "unobserved IPC is unknown");
    chatview::DisplayClient sender;
    expect(sender.start(origin, streaming_token, true, chatview::DisplayAuthentication::Saved,
        chatview::DisplayRole::Streaming), "start approved streaming reporter");
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "create actual gaming HUD");
    chatview::NativeChatConnection gaming(hud, chatview::DisplayRole::Gaming);
    chatview::NativeChatConnectionTestAccess::disable_auto(gaming);
    hud.show_ready();
    bool stream = false, record = false, freeze = false;
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            expect(message.message != WM_QUIT, "HUD unexpectedly exited");
            if (gaming.dispatch(message)) continue;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (!freeze) chatview::publish_obs_output(*transport.state, {GetTickCount64(), stream, record});
        sender.observe_outputs(reader.read_outputs());
        gaming.tick();
    };
    const auto wait = [&](const std::function<bool()> &done, const char *label, ULONGLONG duration = 10000U) {
        const auto until = GetTickCount64() + duration;
        while (!done()) {
            expect(GetTickCount64() < until, label); pump();
            MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    };
    wait([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "HUD ready");
    HWND dialog = chatview::NativeChatConnectionTestAccess::dialog(gaming);
    expect(dialog != nullptr, "connection panel exists");
    chatview::NativeChatConnectionTestAccess::begin(gaming, origin, gaming_token);
    const auto observed = [&](bool streaming, bool recording) {
        const auto &state = chatview::NativeChatConnectionTestAccess::state(gaming);
        return state && state->output.expires_tick > GetTickCount64() &&
            state->output.streaming == streaming && state->output.recording == recording;
    };
    wait([&] { return observed(false, false); }, "reported inactive outputs");
    expect(status_text(dialog).find(L"송출 PC 보고") != std::wstring::npos, "report attribution displayed");
    stream = true; wait([&] { return observed(true, false); }, "stream output start propagates");
    record = true; wait([&] { return observed(true, true); }, "record output start propagates");
    stream = false; wait([&] { return observed(false, true); }, "stream stop retains recording state");
    record = false; wait([&] { return observed(false, false); }, "all output stops propagate");
    expect(status_text(dialog).find(L"영상 제외 미검증") != std::wstring::npos, "output is not clean-video proof");

    command("hang"); stream = true;
    auto &surface = chatview::NativeChatConnectionTestAccess::surface(gaming);
    const auto previous_frames = surface.rendered_frames(), began = GetTickCount64();
    wait([&] { return GetTickCount64() - began >= 4000U; }, "report timeout exercise", 6000U);
    expect(surface.rendered_frames() >= previous_frames + 2U, "slow output HTTP does not block private chat");
    chatview::DisplayUpdate sender_frame;
    expect(sender.take(sender_frame) && sender_frame.status == chatview::DisplayStatus::Receiving,
        "streaming chat receiver remains healthy during output outage");
    expect(sender_frame.envelope.find(L"outputToken") == std::wstring::npos &&
        sender_frame.envelope.find(L"expiresInMs") == std::wstring::npos,
        "output capability and metadata never reach chat HTML");
    command("resume"); wait([&] { return observed(true, false); }, "reporting recovers without chat reauthorization");
    freeze = true;
    const auto stale_began = GetTickCount64();
    wait([&] { return GetTickCount64() - stale_began >= 4000U; }, "frontend observation becomes stale", 6000U);
    command("expire");
    wait([&] {
        const auto &state = chatview::NativeChatConnectionTestAccess::state(gaming);
        return state && state->output.expires_tick == 0U;
    }, "stale reporter becomes unknown");
    expect(status_text(dialog).find(L"OBS 출력: 확인되지 않음") != std::wstring::npos, "unknown output appears in controls");
    const auto silence_began = GetTickCount64();
    wait([&] { return GetTickCount64() - silence_began >= 1200U; }, "stale producer stops reporting", 3000U);
    command("stale-check");
    freeze = false; wait([&] { return observed(true, false); }, "fresh frontend observation restores report");
    expect(sender.sign_out(), "explicit logout requested");
    wait([&] { return !sender.running(); }, "report worker and chat stop before confirmed signout");
    expect(sender.take(sender_frame) && sender_frame.status == chatview::DisplayStatus::SignedOut, "server signout acknowledged");
    wait([&] {
        const auto &state = chatview::NativeChatConnectionTestAccess::state(gaming);
        return state && state->streaming_connections == 0 && state->output.expires_tick == 0U;
    }, "logout retires output authority in same session");
    command("done");
    gaming.close(); hud.destroy();
}
}
int main()
{
    try {
        expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM startup");
        try { exercise(); } catch (...) { CoUninitialize(); throw; }
        CoUninitialize();
        std::cout << "Native output IPC/report/UI/loss/logout flow passed\n"; return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
      catch (...) { std::cerr << "Native output flow failed\n"; return 1; }
}
