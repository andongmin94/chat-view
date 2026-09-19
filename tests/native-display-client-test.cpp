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
};
struct NativeChatSurfaceTestAccess {
    static ICoreWebView2 *core(NativeChatSurface &s) { return s.webview_.Get(); }
};
}
namespace {
void expect(bool value, const char *label) { if (!value) throw std::runtime_error(label); }
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
void gateway_ui(const std::wstring &origin, const std::wstring &ticket)
{
    wchar_t temporary[MAX_PATH]{}; expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary directory");
    const std::wstring profile = std::wstring(temporary) + L"ChatView-Delivery-" + std::to_wstring(GetCurrentProcessId());
    expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE, "isolated WebView2 profile");
    SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str());
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "create production HUD");
    chatview::NativeChatConnection connection(hud);
    hud.show_ready();
    await([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "HUD startup", &connection);
    HWND dialog = chatview::NativeChatConnectionTestAccess::dialog(connection);
    expect(dialog != nullptr, "open native connection panel");
    SetDlgItemTextW(dialog, 101, origin.c_str()); SetDlgItemTextW(dialog, 102, ticket.c_str());
    SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(104, BN_CLICKED), 0);
    expect(GetWindowTextLengthW(GetDlgItem(dialog, 102)) == 0, "connection key cleared from panel");
    auto &surface = chatview::NativeChatConnectionTestAccess::surface(connection);
    await([&] { return surface.rendered_messages() == 1U; }, "WinHTTP gateway frame rendered", &connection);
    Microsoft::WRL::ComPtr<ICoreWebView2> core = chatview::NativeChatSurfaceTestAccess::core(surface);
    expect(evaluate(core.Get(), LR"JS(
        document.querySelector('#messages li b').textContent === '검증 사용자' &&
        document.querySelector('#messages li .content').textContent === '안녕 😀 <img onerror=evil()>' &&
        document.querySelectorAll('#messages img,#messages script').length === 0
    )JS", connection) == L"true", "real DOM Unicode/inert markup");
    // The fixture revokes the grant only after observing this acknowledgement.
    std::cout << "rendered\n" << std::flush;
    await([&] { return !chatview::NativeChatConnectionTestAccess::active(connection); }, "grant revoke ends native delivery", &connection);
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
        expect(client.take(update) && update.envelope.empty(), "stop immediately clears mailbox");
        await([&] { return !client.running(); }, "cancel pending async HTTP", nullptr, 2000U);
        return;
    }
    bool received = false, terminal = false;
    const auto before = GetTickCount64();
    await([&] {
        chatview::DisplayUpdate update;
        if (client.take(update)) {
            if (update.status == chatview::DisplayStatus::Receiving) received = true;
            if (update.status == chatview::DisplayStatus::Ended || update.status == chatview::DisplayStatus::Failed) {
                terminal = update.envelope.empty();
            }
        }
        return !client.running();
    }, "transport terminates", nullptr, mode == "idle" ? 19000U : 10000U);
    chatview::DisplayUpdate update;
    if (client.take(update)) terminal = update.envelope.empty();
    expect(terminal, "terminal status retains no old chat");
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
            try { gateway_ui(origin, ticket); } catch (...) { CoUninitialize(); throw; }
            CoUninitialize();
        } else transport(origin, ticket, argv[1]);
        std::cout << "Native display scenario passed\n"; return 0;
    } catch (const std::exception &error) {
        std::cerr << "Native display test failed: " << error.what() << '\n'; return 1;
    }
}
