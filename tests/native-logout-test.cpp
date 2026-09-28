// SPDX-License-Identifier: GPL-2.0-or-later
// Real NativeChatConnection controls + WinHTTP/WebView2; synthetic provider is
// supplied by native-logout-test.mts. No live-account or video support claim.
#include "hud/native-chat-connection.hpp"
#include "hud/hud-window.hpp"
#include "hud/saved-connection.hpp"
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
    static bool signing_out(NativeChatConnection &c) { return c.signing_out_; }
    static bool running(NativeChatConnection &c) { return c.client_.running(); }
    static bool has_metadata(NativeChatConnection &c) { return c.connection_state_.has_value(); }
    static void disable_auto_connect(NativeChatConnection &c) { c.auto_connect_pending_ = false; }
};
struct NativeChatSurfaceTestAccess {
    static ICoreWebView2 *core(NativeChatSurface &s) { return s.webview_.Get(); }
};
}
namespace {
bool browser_opened = false;
void expect(bool value, const char *label) { if (!value) throw std::runtime_error(label); }
struct Wipe {
    std::wstring &value;
    ~Wipe() { if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); }
};
std::wstring control_text(HWND dialog, int id)
{
    wchar_t value[1024]{}; GetDlgItemTextW(dialog, id, value, 1024); return value;
}
void pump(chatview::NativeChatConnection &connection)
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        expect(message.message != WM_QUIT, "unexpected HUD exit");
        if (connection.dispatch(message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    connection.tick();
}
void await(const std::function<bool()> &predicate, const char *label,
           chatview::NativeChatConnection &connection, ULONGLONG timeout = 15000U)
{
    const auto deadline = GetTickCount64() + timeout;
    while (!predicate()) {
        expect(GetTickCount64() < deadline, label); pump(connection);
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
void click(HWND dialog, int id)
{
    const auto before = GetTickCount64();
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
    expect(GetTickCount64() - before < 200U, "logout/stop button must not wait for network");
}
bool empty_dom(ICoreWebView2 *core, chatview::NativeChatConnection &connection)
{
    struct Result { bool done = false; bool empty = false; };
    const auto result = std::make_shared<Result>();
    expect(SUCCEEDED(core->ExecuteScript(L"document.querySelectorAll('#messages li').length === 0",
        Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [result](HRESULT code, LPCWSTR value) -> HRESULT {
                result->empty = SUCCEEDED(code) && value && std::wstring_view(value) == L"true";
                result->done = true; return S_OK;
            }).Get())), "queue DOM clear assertion");
    await([&] { return result->done; }, "DOM clear callback", connection);
    return result->empty;
}
void run(const std::wstring &origin, const std::wstring &auxiliary, const std::string &mode)
{
    const bool remembered = mode == "remembered" || mode == "storage-failure" || mode == "cancel";
    const bool stored_only = mode == "stored-only";
    wchar_t temporary[MAX_PATH]{}; expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary profile");
    const auto profile = std::wstring(temporary) + L"ChatView-Logout-" + std::to_wstring(GetCurrentProcessId());
    expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE, "isolated profile creation");
    expect(SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str()) != FALSE, "isolated DPAPI storage");
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "production HUD creation");
    chatview::NativeChatConnection connection(hud, chatview::DisplayRole::Streaming, [](HWND, const wchar_t *url) {
        expect(std::wstring_view(url).starts_with(L"http://127.0.0.1:") &&
            std::wstring_view(url).find(L"/login/") != std::wstring_view::npos, "bounded login URL");
        browser_opened = true; std::cout << "browser-opened\n" << std::flush; return true;
    });
    chatview::NativeChatConnectionTestAccess::disable_auto_connect(connection);
    hud.show_ready();
    await([&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "HUD startup", connection);
    const HWND dialog = chatview::NativeChatConnectionTestAccess::dialog(connection);
    expect(dialog != nullptr, "connection panel");
    expect(!chatview::load_connection(), "no saved login before consent");
    Microsoft::WRL::ComPtr<ICoreWebView2> core;
    if (stored_only) {
        expect(chatview::save_connection({origin, auxiliary, true}), "save approved dormant fixture login");
    } else {
        SetDlgItemTextW(dialog, 101, origin.c_str());
        SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_CHECKED, 0);
        SendDlgItemMessageW(dialog, 107, BM_SETCHECK, remembered ? BST_CHECKED : BST_UNCHECKED, 0);
        click(dialog, 104);
        await([&] { return browser_opened; }, "browser login request", connection);
        if (mode != "pending") {
            auto &surface = chatview::NativeChatConnectionTestAccess::surface(connection);
            await([&] { return surface.rendered_messages() == 1U; }, "approved chat rendered", connection);
            core = chatview::NativeChatSurfaceTestAccess::core(surface);
            expect(chatview::NativeChatConnectionTestAccess::has_metadata(connection), "connection metadata before logout");
            auto saved = chatview::load_connection();
            expect(saved.has_value() == remembered, "remember affects persistence only");
            if (saved) { Wipe wipe{saved->credential}; }
        }
    }
    if (mode == "unrelated") expect(chatview::save_connection({origin, auxiliary, true}), "retain a different saved creator");
    chatview::UniqueHandle storage_lock;
    if (mode == "storage-failure") {
        const auto path = profile + L"\\ChatView\\saved-connection.bin";
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        expect(file != INVALID_HANDLE_VALUE, "readable credential file locked against deletion"); storage_lock.reset(file);
    }
    if (mode == "stopped") click(dialog, 105); // Intentionally no wait before logout.
    click(dialog, 108);
    expect(!chatview::NativeChatConnectionTestAccess::active(connection) &&
        !chatview::NativeChatConnectionTestAccess::has_metadata(connection), "logout clears local state immediately");
    expect(control_text(dialog, 110).find(L"확인되지 않음") != std::wstring::npos, "no stale session label");
    // Duplicate clicks and stop must not issue another request or cancel logout.
    click(dialog, 108); click(dialog, 105);
    if (mode == "cancel") {
        std::string requested; std::getline(std::cin, requested);
        expect(requested == "signout-requested", "pending server revocation observed");
        const auto before = GetTickCount64(); connection.close();
        expect(GetTickCount64() - before < 200U, "shutdown does not wait on server revocation");
        await([&] { return !chatview::NativeChatConnectionTestAccess::running(connection); }, "cancel signout I/O", connection, 2000U);
        expect(!chatview::load_connection(), "cancelled network logout never restores remembered login");
        hud.destroy(); return;
    }
    await([&] { return !chatview::NativeChatConnectionTestAccess::signing_out(connection); }, "logout result", connection);
    if (mode == "retry" || mode == "storage-failure") {
        expect(control_text(dialog, 106).find(L"미확인") != std::wstring::npos, "failure is not a success message");
        if (mode == "storage-failure") {
            auto saved = chatview::load_connection(); expect(saved.has_value(), "failed deletion remains visible");
            Wipe wipe{saved->credential}; storage_lock.reset();
        } else expect(!chatview::load_connection(), "non-remembered retry is not saved to disk");
        std::cout << "retry-ready\n" << std::flush;
        click(dialog, 108);
        await([&] { return !chatview::NativeChatConnectionTestAccess::signing_out(connection); }, "explicit logout retry", connection);
    }
    const auto notice = control_text(dialog, 106);
    if (mode == "pending") {
        expect(notice.find(L"미확인") != std::wstring::npos, "uncollected approval cannot claim server logout");
        expect(notice.find(L"서버 승인도 해제했습니다") == std::wstring::npos, "no fake confirmation");
    } else expect(notice.find(L"서버 승인도 해제했습니다") != std::wstring::npos, "success requires a server acknowledgement");
    if (mode == "unrelated") {
        auto saved = chatview::load_connection(); expect(saved && saved->credential == auxiliary, "unrelated saved approval is preserved");
        Wipe wipe{saved->credential};
        expect(notice.find(auxiliary) == std::wstring::npos, "credentials never appear in notices");
    } else expect(!chatview::load_connection(), "logout leaves no matching DPAPI login");
    if (core) await([&] { return empty_dom(core.Get(), connection); }, "logout clears actual private chat DOM", connection);
    for (int i = 0; i < 20; ++i) { pump(connection); Sleep(10); }
    expect(!chatview::NativeChatConnectionTestAccess::active(connection) &&
        !chatview::NativeChatConnectionTestAccess::running(connection), "no automatic reapproval after logout");
    connection.close(); hud.destroy();
}
}
int main(int argc, char **argv)
{
    try {
        expect(argc == 2, "scenario required");
        std::string origin_input, auxiliary_input;
        std::getline(std::cin, origin_input); std::getline(std::cin, auxiliary_input);
        expect(origin_input.size() < 2048U && auxiliary_input.size() == 64U, "bounded inherited fixture input");
        std::wstring origin(origin_input.begin(), origin_input.end()), auxiliary(auxiliary_input.begin(), auxiliary_input.end());
        Wipe wipe{auxiliary};
        expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM startup");
        try { run(origin, auxiliary, argv[1]); } catch (...) { CoUninitialize(); throw; }
        CoUninitialize(); std::cout << "Native logout scenario passed\n"; return 0;
    } catch (const std::exception &error) { std::cerr << "Native logout test failed: " << error.what() << '\n'; return 1; }
      catch (...) { std::cerr << "Native logout test failed\n"; return 1; }
}
