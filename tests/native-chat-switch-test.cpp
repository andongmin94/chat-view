// SPDX-License-Identifier: GPL-2.0-or-later
// Real HUD/WebView2 and WinHTTP; provider, external page and consent are fixtures.
#include "hud/display-client.hpp"
#include "hud/hud-window.hpp"
#include "hud/native-chat-connection.hpp"
#include "hud/saved-connection.hpp"
#include "common/chat-config.hpp"
#include "common/window-messages.hpp"
#include "common/win32-handle.hpp"
#include "control-center-driver.hpp"
#include <Windows.h>
#include <objbase.h>
#include <wrl/event.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace chatview {
struct NativeChatConnectionTestAccess {
    static HWND host(NativeChatConnection &c) { return c.host_; }
    static HWND dialog(NativeChatConnection &c) { return c.dialog_; }
    static bool active(NativeChatConnection &c) { return c.active_; }
    static bool signing_out(NativeChatConnection &c) { return c.signing_out_; }
    static DisplayClient &client(NativeChatConnection &c) { return c.client_; }
    static NativeChatSurface &surface(NativeChatConnection &c) { return c.surface_; }
    static auto membership(NativeChatConnection &c) { return c.connection_state_; }
};
struct NativeChatSurfaceTestAccess {
    static ICoreWebView2 *core(NativeChatSurface &s) { return s.webview_.Get(); }
};
}
namespace {
using Access = chatview::NativeChatConnectionTestAccess;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
using Status = chatview::NativeChatStatus;
std::unique_ptr<ControlCenterDriver> center;
std::wstring management_url;
unsigned management_attempts = 0, management_opened = 0;
bool fail_management = false;
constexpr wchar_t kExternal[] = L"https://www.youtube.com/live_chat?is_popout=1&v=chatview123";
void expect(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void pump(chatview::NativeChatConnection &chat)
{
    if (center) center->pulse();
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        expect(message.message != WM_QUIT, "HUD stays alive");
        if (chat.dispatch(message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    chat.tick();
}
void await(chatview::NativeChatConnection &chat, const std::function<bool()> &done,
           const char *message, ULONGLONG duration = 15000U)
{
    const auto deadline = GetTickCount64() + duration;
    while (!done()) {
        expect(GetTickCount64() < deadline, message); pump(chat);
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
std::wstring text(HWND dialog, int id)
{
    wchar_t value[1024]{}; GetDlgItemTextW(dialog, id, value, 1024); return value;
}
void expect_status(chatview::NativeChatConnection &chat, Status expected)
{
    const UINT query = RegisterWindowMessageW(chatview::kQueryNativeChatMessageName);
    expect(query != 0, "register current display query");
    await(chat, [&] {
        const auto status = chatview::decode_native_chat_status(static_cast<std::uint64_t>(
            SendMessageW(Access::host(chat), query, 0, 0)));
        return status == expected && text(Access::dialog(chat), 113) == chatview::native_chat_status_text_ko(expected) &&
            (IsWindowEnabled(GetDlgItem(Access::dialog(chat), 112)) != FALSE) == chatview::can_request_native_chat_return(expected) &&
            (!center || center->matches(expected));
    }, "Control Center, native status and return button agree");
    if (center) expect(center->video_warning(), "Control Center keeps independent video warning");
    const HWND label = GetDlgItem(Access::dialog(chat), 113);
    RECT client{}; expect(GetClientRect(label, &client) != FALSE, "display status bounds");
    const auto value = text(Access::dialog(chat), 113);
    const HDC dc = GetDC(label); expect(dc != nullptr, "display text measurement");
    const auto font = reinterpret_cast<HFONT>(SendMessageW(label, WM_GETFONT, 0, 0));
    const auto old = font ? SelectObject(dc, font) : nullptr;
    RECT required{0, 0, client.right, 0};
    const int height = DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &required,
        DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    if (old && old != HGDI_ERROR) SelectObject(dc, old);
    ReleaseDC(label, dc);
    expect(height > 0 && required.bottom <= client.bottom && required.right <= client.right,
        "display status fits the actual native control/font");
}
std::wstring evaluate(ICoreWebView2 *core, const wchar_t *script, chatview::NativeChatConnection &chat)
{
    struct Result { bool done = false; HRESULT status = E_FAIL; std::wstring value; };
    auto result = std::make_shared<Result>();
    expect(SUCCEEDED(core->ExecuteScript(script, Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [result](HRESULT status, LPCWSTR value) -> HRESULT {
            result->status = status; if (value) result->value = value; result->done = true; return S_OK;
        }).Get())), "queue DOM inspection");
    await(chat, [&] { return result->done; }, "DOM inspection completes");
    expect(SUCCEEDED(result->status), "DOM inspection succeeds"); return result->value;
}
void command(HWND dialog, int id) { SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0); }
void management(chatview::NativeChatConnection &chat, ICoreWebView2 *core, bool fail)
{
    const HWND dialog = Access::dialog(chat);
    await(chat, [&] { return IsWindowEnabled(GetDlgItem(dialog, 114)) != FALSE; }, "management action becomes available");
    const bool active = Access::active(chat);
    const auto before = Access::membership(chat);
    LPWSTR uri = nullptr;
    expect(SUCCEEDED(core->get_Source(&uri)) && uri, "document identity before management");
    const std::wstring original(uri); CoTaskMemFree(uri);
    const auto attempts = management_attempts, opened = management_opened;
    fail_management = fail;
    command(dialog, 114);
    expect(management_attempts == attempts + 1 && management_opened == opened + (fail ? 0U : 1U),
        "one fixed management URL launch per explicit request");
    expect(text(dialog, 106).find(fail ? L"열지 못했습니다" : L"열기를 요청했습니다") != std::wstring::npos,
        "launcher failure is not reported as successful navigation or authentication");
    expect(Access::active(chat) == active && Access::membership(chat).has_value() == before.has_value(),
        "management does not replace display or account state");
    if (before) expect(Access::membership(chat)->membership == before->membership, "management preserves exact approval");
    uri = nullptr;
    expect(SUCCEEDED(core->get_Source(&uri)) && uri, "document identity after management");
    const bool unchanged = original == uri; CoTaskMemFree(uri);
    expect(unchanged, "management never navigates the private or external chat WebView");
}
void management_scope(HWND dialog)
{
    const HWND scope = GetDlgItem(dialog, 115);
    const auto value = text(dialog, 115);
    expect(scope && IsWindowVisible(scope) && value.find(L"브라우저의 로그인 계정") != std::wstring::npos &&
        value.find(L"HUD 캡처 보호 대상이 아닙니다") != std::wstring::npos, "separate browser identity/privacy notice");
    RECT bounds{}; expect(GetClientRect(scope, &bounds) != FALSE, "management scope bounds");
    const HDC dc = GetDC(scope); expect(dc != nullptr, "management scope font DC");
    const auto font = reinterpret_cast<HFONT>(SendMessageW(scope, WM_GETFONT, 0, 0));
    const auto old = font ? SelectObject(dc, font) : nullptr;
    RECT needed{0, 0, bounds.right, 0};
    const int height = DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &needed,
        DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    if (old && old != HGDI_ERROR) SelectObject(dc, old);
    ReleaseDC(scope, dc);
    expect(height > 0 && needed.bottom <= bounds.bottom && needed.right <= bounds.right,
        "browser identity/privacy notice fits its real font and control");
}
// Supply a fixed external-origin document without changing navigation policy,
// disabling TLS checks, making internet requests or adding production hooks.
EventRegistrationToken mock_external(ICoreWebView2 *core)
{
    ComPtr<ICoreWebView2_2> second;
    expect(SUCCEEDED(core->QueryInterface(IID_PPV_ARGS(&second))), "WebView2 environment interface");
    ComPtr<ICoreWebView2Environment> environment;
    expect(SUCCEEDED(second->get_Environment(&environment)), "existing WebView2 environment");
    expect(SUCCEEDED(core->AddWebResourceRequestedFilter(kExternal, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT)), "external fixture filter");
    EventRegistrationToken token{};
    expect(SUCCEEDED(core->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
        [environment](ICoreWebView2 *, ICoreWebView2WebResourceRequestedEventArgs *args) -> HRESULT {
            ComPtr<ICoreWebView2WebResourceRequest> request;
            if (FAILED(args->get_Request(&request))) return E_FAIL;
            LPWSTR uri = nullptr;
            const bool matches = SUCCEEDED(request->get_Uri(&uri)) && uri && std::wstring(uri) == kExternal;
            CoTaskMemFree(uri); if (!matches) return S_OK;
            constexpr char html[] = "<!doctype html><meta charset='utf-8'><div id='external-marker'>External fixture</div>"
                "<script>globalThis.privateFrames=0;window.chrome.webview.addEventListener('message',e=>{"
                "if(e.data&&e.data.type==='chat-snapshot')++globalThis.privateFrames;});</script>";
            ComPtr<IStream> stream;
            if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return E_FAIL;
            ULONG written = 0;
            if (FAILED(stream->Write(html, static_cast<ULONG>(sizeof(html) - 1U), &written)) || written != static_cast<ULONG>(sizeof(html) - 1U))
                return E_FAIL;
            LARGE_INTEGER zero{};
            if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr))) return E_FAIL;
            ComPtr<ICoreWebView2WebResourceResponse> response;
            const auto result = environment->CreateWebResourceResponse(stream.Get(), 200, L"OK",
                L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Security-Policy: default-src 'none'; script-src 'unsafe-inline'",
                &response);
            return SUCCEEDED(result) ? args->put_Response(response.Get()) : result;
        }).Get(), &token)), "external fixture response");
    return token;
}
void external(chatview::NativeChatConnection &chat, ICoreWebView2 *core)
{
    if (center) {
        center->edit_external(kExternal);
        expect_status(chat, Status::Receiving); // Editing settings does not change the actual display.
        center->apply_external();
        await(chat, [&] { return !Access::active(chat); }, "real Control Center applies external page");
    } else {
        expect(chatview::save_chat_config({kExternal}), "save existing normalized external configuration");
        const UINT change = RegisterWindowMessageW(chatview::kConfigChangedMessageName);
        expect(change != 0, "existing apply message");
        SendMessageW(Access::host(chat), change, 0, 0);
    }
    expect(!Access::active(chat) && !Access::surface(chat).ready() && !Access::membership(chat),
        "external apply synchronously fences delivery before navigation");
    await(chat, [&] {
        return !Access::client(chat).running() && evaluate(core,
            L"document.querySelector('#external-marker')?.textContent === 'External fixture'", chat) == L"true";
    }, "external page loads after display stop");
    expect(evaluate(core, L"privateFrames === 0", chat) == L"true", "no private snapshot reaches the external document");
    expect_status(chat, Status::ExternalPageResumable);
}
void run(const std::wstring &origin, const std::wstring &other, const std::string &mode,
         const std::filesystem::path &config_executable)
{
    wchar_t temporary[MAX_PATH]{}; expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary root");
    const auto profile = std::wstring(temporary) + L"ChatView-Switch-" + std::to_wstring(GetCurrentProcessId());
    expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE, "isolated profile");
    expect(SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str()) != FALSE, "isolated storage");
    chatview::UniqueHandle ready(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    chatview::HudWindow hud;
    expect(hud.create(GetModuleHandleW(nullptr), ready.get()), "create real HUD");
    management_url = origin + L"/account";
    chatview::NativeChatConnection chat(hud, chatview::DisplayRole::Streaming, [](HWND window, const wchar_t *url) {
        const std::wstring target(url);
        if (target == management_url) {
            ++management_attempts;
            if (fail_management) return false;
            ++management_opened;
            // A reentrant launcher must not recursively open another browser.
            command(window, 114);
            std::cout << "management-opened\n" << std::flush; return true;
        }
        expect(target.starts_with(management_url.substr(0, management_url.size() - 8U) + L"/login/"),
            "browser launch is the exact service account route or existing login intent");
        std::cout << "browser-opened\n" << std::flush; return true;
    });
    hud.show_ready();
    await(chat, [&] { return WaitForSingleObject(ready.get(), 0) == WAIT_OBJECT_0; }, "HUD startup");
    if (mode == "memory") {
        center = std::make_unique<ControlCenterDriver>(); center->start(config_executable);
        await(chat, [&] { return center->matches(Status::Idle) && IsWindowEnabled(GetDlgItem(center->window(), 1009)); },
            "actual Control Center finds production HUD");
        center->open_panel();
        await(chat, [&] { return Access::dialog(chat) && IsWindowVisible(Access::dialog(chat)); }, "Control Center opens existing native panel");
        expect(!std::filesystem::exists(std::filesystem::path(profile) / L"ChatView" / L"config.ini"),
            "open/query never saves an external URL");
    } else expect(chat.open_dialog(), "existing connection panel");
    const HWND dialog = Access::dialog(chat);
    expect(GetDlgItem(dialog, 112) != nullptr, "explicit current-approval return button");
    expect_status(chat, Status::Idle);
    expect((GetWindowLongPtrW(dialog, GWL_EXSTYLE) & WS_EX_LAYERED) != 0, "protected panel uses documented layered affinity query");
    BYTE alpha = 0; DWORD layer_flags = 0;
    expect(GetLayeredWindowAttributes(dialog, nullptr, &alpha, &layer_flags) && alpha == 255 && layer_flags == LWA_ALPHA,
        "connection controls stay fully opaque");
    management_scope(dialog);
    command(dialog, 114);
    expect(management_attempts == 0 && !IsWindowEnabled(GetDlgItem(dialog, 114)), "no current connection cannot launch management");
    command(dialog, 112);
    expect(!Access::client(chat).running(), "return without a current approval does not log in");
    SetDlgItemTextW(dialog, 101, origin.c_str());
    SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageW(dialog, 107, BM_SETCHECK, mode == "remembered" ? BST_CHECKED : BST_UNCHECKED, 0);
    command(dialog, 104);
    await(chat, [&] { return Access::surface(chat).rendered_messages() == 1U; }, "initial private chat renders");
    expect_status(chat, Status::Receiving);
    const auto initial = Access::membership(chat);
    expect(initial.has_value(), "initial approved membership");
    ComPtr<ICoreWebView2> core = chatview::NativeChatSurfaceTestAccess::core(Access::surface(chat));
    const auto resource = mock_external(core.Get());
    management(chat, core.Get(), true);
    management(chat, core.Get(), false);
    expect(Access::surface(chat).ready(), "browser launch failure/success leave private chat ready");
    if (mode == "unrelated") expect(chatview::save_connection({origin, other, true}), "unrelated saved account fixture");
    external(chat, core.Get());
    expect(Access::client(chat).can_resume_current(), "ordinary external switch retains current approval");
    if (center) {
        // Empty Apply chooses setup, not a falsely reported external page. The
        // retained approval still permits a request, without creating new consent.
        center->edit_external(L""); center->apply_external();
        expect_status(chat, Status::IdleResumable);
        center->edit_external(kExternal); center->apply_external();
        expect_status(chat, Status::ExternalPageResumable);
        await(chat, [&] { return evaluate(core.Get(), L"typeof privateFrames === 'number'", chat) == L"true"; }, "external fixture returns");
        SendMessageW(dialog, WM_CLOSE, 0, 0);
        DWORD affinity = 0;
        expect(GetWindowDisplayAffinity(dialog, &affinity) && affinity == WDA_EXCLUDEFROMCAPTURE,
            "hidden panel retains readable exclusion without resetting it");
        center->open_panel();
        try {
            await(chat, [&] { return IsWindowVisible(dialog); }, "same native panel reopens from Control Center");
        } catch (...) {
            // Test-only bounded phase/flags, never URLs, tokens or chat text.
            DWORD current_affinity = 0;
            const BOOL queried = GetWindowDisplayAffinity(dialog, &current_affinity);
            std::cerr << "Reopen state: query=" << queried << " affinity=" << current_affinity
                << " visible=" << IsWindowVisible(dialog) << " enabled=" << IsWindowEnabled(GetDlgItem(center->window(), 1009)) << '\n';
            throw;
        }
        expect(Access::dialog(chat) == dialog, "Control Center reuses one panel and approval");
    }
    // Wait for the server to inspect the stopped connection/revoke when needed.
    std::cout << "external-ready\n" << std::flush;
    std::string next; std::getline(std::cin, next); expect(next == "continue", "server transition acknowledged");
    expect(evaluate(core.Get(), L"privateFrames === 0", chat) == L"true", "late server changes do not enter external document");
    // Edited controls and unrelated stored credentials must not retarget return.
    SetDlgItemTextW(dialog, 101, L"https://not-the-current-service.invalid");
    SendDlgItemMessageW(dialog, 103, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageW(dialog, 107, BM_SETCHECK, BST_CHECKED, 0);
    management(chat, core.Get(), false);
    management_scope(dialog);
    expect(evaluate(core.Get(), L"privateFrames === 0", chat) == L"true", "management does not publish to the external page");
    command(dialog, 112); command(dialog, 112);
    if (mode == "revoked" || mode == "mismatch") {
        await(chat, [&] { return !Access::client(chat).running() && !Access::active(chat); }, "invalid resumed approval stops");
        expect(!Access::membership(chat) && Access::surface(chat).rendered_messages() == 0,
            "revoked or substituted membership never publishes private chat");
        if (mode == "revoked") {
            expect(!Access::client(chat).can_resume_current(), "server denial removes return authority");
            expect_status(chat, Status::Idle);
            command(dialog, 114);
            expect(management_attempts == 3 && !IsWindowEnabled(GetDlgItem(dialog, 114)), "confirmed revocation disables current-connection entry");
        }
    } else {
        await(chat, [&] { return Access::surface(chat).rendered_messages() == 1U; }, "same approval returns to native chat");
        expect_status(chat, Status::Receiving);
        expect(Access::membership(chat)->membership == initial->membership, "exact role/session/connection survives return");
        auto saved = chatview::load_connection();
        if (mode == "remembered") expect(saved.has_value(), "remembered connection preserved");
        else if (mode == "unrelated") expect(saved && saved->credential == other, "return preserves unrelated stored account");
        else expect(!saved, "return never persists memory-only approval despite edited checkbox");
        if (saved) SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
        command(dialog, 105);
        await(chat, [&] { return !Access::client(chat).running(); }, "ordinary stop completes", 2000U);
        expect_status(chat, Status::IdleResumable);
        command(dialog, 112);
        await(chat, [&] { return Access::surface(chat).rendered_messages() == 1U; }, "return after ordinary stop");
        expect_status(chat, Status::Receiving);
        expect(Access::membership(chat)->membership == initial->membership, "stop/return preserves membership");
        if (center) external(chat, core.Get());
        command(dialog, 108);
        await(chat, [&] { return !Access::signing_out(chat) && !Access::client(chat).running(); }, "explicit logout completes");
        expect(!Access::client(chat).can_resume_current(), "logout intent cannot be undone by return");
        expect_status(chat, center ? Status::ExternalPage : Status::Idle);
        command(dialog, 114);
        expect(management_attempts == 3 && !IsWindowEnabled(GetDlgItem(dialog, 114)), "logout intent does not launch or recreate browser authority");
        command(dialog, 112);
        expect(!Access::client(chat).running(), "return after logout never renews or opens a browser");
        if (mode == "logout-failure") {
            command(dialog, 108);
            await(chat, [&] { return !Access::signing_out(chat) && !Access::client(chat).running(); }, "explicit logout retry");
            expect_status(chat, Status::Idle);
        }
    }
    expect(text(dialog, 111).find(L"수신 영상의 HUD 제외: 미검증") != std::wstring::npos,
        "switching never changes the video verification boundary");
    expect(management_opened == 2 && management_attempts == 3, "only explicit eligible management requests reached the launcher");
    if (center) {
        center->request_close(); await(chat, [&] { return center->exited(); }, "Control Center closes independently", 5000U);
        expect(center->succeeded(), "Control Center exits successfully"); center.reset();
    }
    core->remove_WebResourceRequested(resource);
    chat.close(); hud.destroy();
}
}
int main(int argc, char **argv)
{
    try {
        expect(argc == 3, "test mode and Control Center executable");
        std::string origin, other;
        std::getline(std::cin, origin); std::getline(std::cin, other);
        expect(origin.size() < 2048U && other.size() == 64U, "bounded fixture input");
        expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM startup");
        try { run(std::wstring(origin.begin(), origin.end()), std::wstring(other.begin(), other.end()), argv[1],
            std::filesystem::absolute(argv[2])); }
        catch (...) { center.reset(); CoUninitialize(); throw; }
        CoUninitialize();
        std::cout << "Native external-page switch, local presentation and exact-approval return passed\n"; return 0;
    } catch (const std::exception &error) {
        std::cerr << "Native chat switch failed: " << error.what() << '\n'; return 1;
    }
}
