// SPDX-License-Identifier: GPL-2.0-or-later
// Launched by the Node fixture; synthetic keys arrive only over inherited stdin.
#include "hud/display-client.hpp"
#include "hud/hud-window.hpp"
#include "hud/native-chat-connection.hpp"
#include "common/win32-handle.hpp"
#include <Windows.h>
#include <wrl/event.h>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace chatview {
struct NativeChatConnectionTestAccess {
    static HWND dialog(NativeChatConnection &c) { c.open_dialog(); return c.dialog_; }
    static NativeChatSurface &surface(NativeChatConnection &c) { return c.surface_; }
    static bool active(NativeChatConnection &c) { return c.active_; }
    static const std::optional<DisplayConnectionState> &connection(NativeChatConnection &c) { return c.connection_state_; }
};
struct NativeChatSurfaceTestAccess {
    static ICoreWebView2 *core(NativeChatSurface &s) { return s.webview_.Get(); }
};
}
namespace {
void expect(bool value, const char *label) { if (!value) throw std::runtime_error(label); }
std::wstring control_text(HWND dialog, int id)
{
    wchar_t value[1024]{};
    GetDlgItemTextW(dialog, id, value, 1024);
    return value;
}
void pump(chatview::NativeChatConnection *connection = nullptr)
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        expect(message.message != WM_QUIT, "HUD terminated unexpectedly");
        if (connection && connection->dispatch(message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    if (connection) connection->tick();
}
void await(const std::function<bool()> &predicate, const char *label,
           chatview::NativeChatConnection *connection = nullptr, ULONGLONG timeout = 15000U)
{
    const ULONGLONG deadline = GetTickCount64() + timeout;
    while (!predicate()) {
        expect(GetTickCount64() < deadline, label); pump(connection);
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
std::wstring evaluate(ICoreWebView2 *core, const wchar_t *script, chatview::NativeChatConnection &connection)
{
    struct Result { bool done = false; HRESULT code = E_FAIL; std::wstring value; };
    const auto result = std::make_shared<Result>();
    expect(SUCCEEDED(core->ExecuteScript(script,
        Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [result](HRESULT code, LPCWSTR value) -> HRESULT {
                result->code = code; if (value) result->value = value; result->done = true; return S_OK;
            }).Get())), "queue DOM assertion");
    await([&] { return result->done; }, "DOM assertion completion", &connection);
    expect(SUCCEEDED(result->code), "DOM assertion succeeded"); return result->value;
}
void validate_inputs(const std::wstring &origin, const std::wstring &ticket)
{
    chatview::DisplayClient client;
    for (const auto &bad : {L"http://localhost:47831", L"http://example.com", L"https://user:pass@example.com",
             L"https://example.com/path", L"https://example.com?key=x", L"https://example.com/#x",
             L"file:///test", L"https://example.com\\path", L"https://example.com/%2e", L" https://example.com"}) {
        expect(!client.start(bad, ticket, true), "reject unsafe origin without connecting");
    }
    expect(!client.start(origin, ticket), "plain loopback requires explicit opt-in");
    expect(!client.start(origin, L"not-a-ticket", true), "bad ticket rejected before I/O");
}
void gateway_ui(const std::wstring &origin)
{
    wchar_t temporary[MAX_PATH]{}; expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary directory");
    const std::wstring profile = std::wstring(temporary) + L"ChatView-Delivery-" + std::to_wstring(GetCurrentProcessId());
    expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE, "isolated WebView2 profile");
    SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str());
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "create production HUD");
    chatview::NativeChatConnection connection(hud, chatview::DisplayRole::Streaming, [](HWND, const wchar_t *url) {
        const std::wstring value(url);
        expect(value.starts_with(L"http://127.0.0.1:") && value.find(L"/login/") != std::wstring::npos,
               "browser launch uses only the validated service login URL");
        std::cout << "browser-opened\n" << std::flush; return true;
    });
    hud.show_ready();
    await([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "HUD startup", &connection);
    HWND dialog = chatview::NativeChatConnectionTestAccess::dialog(connection);
    expect(dialog != nullptr, "open native connection panel");
    expect(control_text(dialog, 109).find(L"송출 PC") != std::wstring::npos, "panel identifies OBS runtime role before consent");
    expect(control_text(dialog, 110).find(L"확인되지 않음") != std::wstring::npos, "no session claim before authorization");
    SetDlgItemTextW(dialog, 101, origin.c_str());
    SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(104, BN_CLICKED), 0);
    expect(GetDlgItem(dialog, 102) == nullptr && GetDlgItem(dialog, 107) != nullptr, "login panel has no manual key input");
    auto &surface = chatview::NativeChatConnectionTestAccess::surface(connection);
    await([&] { return surface.rendered_messages() == 1U; }, "WinHTTP gateway frame rendered", &connection);
    const auto &state = chatview::NativeChatConnectionTestAccess::connection(connection);
    expect(state && state->membership.role == chatview::DisplayRole::Streaming &&
        state->gaming_connections == 0 && state->streaming_connections == 1,
        "native controls receive authorized role and same-session display counts");
    expect(control_text(dialog, 110) == chatview::connection_summary(*state), "panel displays actual accepted session metadata");
    expect(control_text(dialog, 110).find(L"영상 제외 미검증") != std::wstring::npos, "role connection is not capture qualification");
    Microsoft::WRL::ComPtr<ICoreWebView2> core = chatview::NativeChatSurfaceTestAccess::core(surface);
    expect(evaluate(core.Get(), LR"JS(
        document.querySelector('#messages li b').textContent === '검증 사용자' &&
        document.querySelector('#messages li .content').textContent === '안녕 😀 <img onerror=evil()>' &&
        document.querySelectorAll('#messages img,#messages script').length === 0
    )JS", connection) == L"true", "real DOM Unicode/inert markup");
    std::cout << "rendered\n" << std::flush;
    await([&] { return !chatview::NativeChatConnectionTestAccess::active(connection); }, "grant revoke ends native delivery", &connection);
    expect(!chatview::NativeChatConnectionTestAccess::connection(connection), "revocation clears native session state");
    expect(control_text(dialog, 110).find(L"확인되지 않음") != std::wstring::npos, "revocation clears session status text");
    await([&] { return evaluate(core.Get(), L"document.querySelectorAll('#messages li').length === 0", connection) == L"true"; },
          "revocation clears actual DOM", &connection);
    connection.close(); hud.destroy();
}
void transport(const std::wstring &origin, const std::wstring &ticket, const std::string &mode)
{
    chatview::DisplayClient client;
    expect(client.start(origin, ticket, true), "start read-only native client");
    if (mode == "cancel") {
        std::string requested; std::getline(std::cin, requested);
        expect(requested == "requested", "fixture has a pending HTTP exchange");
        const auto before = GetTickCount64(); client.stop();
        expect(GetTickCount64() - before < 200U, "stop does not wait for network");
        chatview::DisplayUpdate update;
        expect(client.take(update) && update.envelope.empty() && !update.connection, "stop immediately clears chat and status mailbox");
        await([&] { return !client.running(); }, "cancel pending async HTTP", nullptr, 2000U);
        return;
    }
    bool received = false, terminal = false;
    const auto before = GetTickCount64();
    await([&] {
        chatview::DisplayUpdate update;
        if (client.take(update)) {
            if (update.status == chatview::DisplayStatus::Receiving) {
                received = true;
                expect(update.connection.has_value(), "receiving requires validated connection metadata");
                expect(update.envelope.find(L"broadcastSessionId") == std::wstring::npos &&
                    update.envelope.find(L"connectionId") == std::wstring::npos,
                    "connection metadata does not reach the renderer");
            }
            if (update.status == chatview::DisplayStatus::Ended || update.status == chatview::DisplayStatus::Failed ||
                update.status == chatview::DisplayStatus::RoleMismatch) {
                terminal = update.envelope.empty() && !update.connection;
            }
        }
        return !client.running();
    }, "transport terminates", nullptr, mode == "idle" ? 19000U : 10000U);
    chatview::DisplayUpdate update;
    if (client.take(update)) terminal = update.envelope.empty() && !update.connection;
    expect(terminal, "terminal status retains no old chat or session metadata");
    if (mode == "local-expiry") {
        expect(received, "fixture delivered before expiry");
        expect(GetTickCount64() - before < 4000U, "local lease expires despite continuing server frames");
    } else if (mode != "idle") expect(!received, "invalid transport never publishes a display frame");
}
}
int main(int argc, char **argv)
{
    try {
        expect(argc == 2, "scenario required");
        std::string origin_line, ticket_line;
        std::getline(std::cin, origin_line); std::getline(std::cin, ticket_line);
        expect(origin_line.size() < 2048U && ticket_line.size() == 64U, "bounded fixture input");
        const std::wstring origin(origin_line.begin(), origin_line.end()), ticket(ticket_line.begin(), ticket_line.end());
        validate_inputs(origin, ticket);
        if (std::string(argv[1]) == "gateway") {
            expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM startup");
            try { gateway_ui(origin); } catch (...) { CoUninitialize(); throw; }
            CoUninitialize();
        } else transport(origin, ticket, argv[1]);
        std::cout << "Native display scenario passed\n"; return 0;
    } catch (const std::exception &error) {
        std::cerr << "Native display test failed: " << error.what() << '\n'; return 1;
    }
}
