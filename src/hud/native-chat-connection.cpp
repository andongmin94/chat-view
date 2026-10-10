// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/native-chat-connection.hpp"
#include "hud/saved-connection.hpp"
#include "hud/hud-window.hpp"
#include <shellapi.h>
#include <commctrl.h>
#include <string_view>
#include <utility>

namespace chatview {
namespace {
constexpr int kConnectHotkey = 0x4348;
constexpr int kOrigin = 101, kLocal = 103, kConnect = 104, kDisconnect = 105, kNotice = 106;
constexpr int kRemember = 107, kForget = 108, kRole = 109, kSession = 110, kVideoScope = 111, kResume = 112, kDisplayStatus = 113;
constexpr int kManage = 114, kManagementScope = 115;
constexpr wchar_t kClass[] = L"ChatView.NativeConnection";
// This UI does not keep the last server socket counts after a native stop.
// Local resume eligibility is not a claim that the peer remains connected.
constexpr const wchar_t *unavailable_connection_text(NativeChatStatus status) noexcept
{
    if (can_request_native_chat_return(status))
        return L"서버 채팅 연결 수: 현재 미확인 (표시 중지)\n"
            L"원래 승인: 같은 역할로 복귀 요청 가능\n영상 제외: 미검증";
    if (status == NativeChatStatus::Stopping || status == NativeChatStatus::SigningOut)
        return L"서버 채팅 연결 수: 종료 확인 중\n"
            L"복귀: 작업 종료까지 대기\n영상 제외: 미검증";
    return L"서버 채팅 연결 수: 현재 미확인\n"
        L"현재 복귀 승인: 없음 또는 미확인\n영상 제외: 미검증";
}
std::wstring text(HWND parent, int id, int maximum)
{
    const HWND control = GetDlgItem(parent, id);
    const int length = GetWindowTextLengthW(control);
    if (length <= 0 || length > maximum) return {};
    std::wstring result(static_cast<size_t>(length) + 1U, L'\0');
    const int read = GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<size_t>(read)); return result;
}
}
NativeChatConnection::NativeChatConnection(HudWindow &hud, DisplayRole role, BrowserLauncher browser) noexcept
    : hud_(hud), browser_(browser ? browser : [](HWND window, const wchar_t *url) {
        return reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", url, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }), role_(role)
{
    open_message_ = RegisterWindowMessageW(kOpenNativeChatMessageName);
    query_message_ = RegisterWindowMessageW(kQueryNativeChatMessageName);
    // This component and the HUD share an owner thread. Use the system's
    // subclass chain rather than replacing HudWindow's procedure or input path.
    if (open_message_ && query_message_ && hud_.window_ &&
        SetWindowSubclass(hud_.window_, host_procedure, kConnectHotkey,
                          reinterpret_cast<DWORD_PTR>(this))) host_ = hud_.window_;
    hotkey_ = RegisterHotKey(nullptr, kConnectHotkey, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'C') != FALSE;
    if (!hotkey_) OutputDebugStringW(L"[ChatView HUD] Native chat connection hotkey unavailable\n");
}
NativeChatConnection::~NativeChatConnection() { close(); }
NativeChatStatus NativeChatConnection::local_status() const noexcept
{
    if (closed_ || !host_ || !hud_.webview_ready_) return NativeChatStatus::Unavailable;
    if (signing_out_) return NativeChatStatus::SigningOut;
    if (hud_.shutting_down_ || hud_.system_suppressed() || hud_.capture_exclusion_failed_ || hud_.capture_risk_ ||
        !hud_.capture_exclusion_intact()) return NativeChatStatus::Paused;
    if (!active_) return inactive_native_chat_status(client_.running(),
        hud_.webview_.external_page_selected(), client_.can_resume_current());
    if (awaiting_login_) return NativeChatStatus::AwaitingApproval;
    if (reconnecting_ || !client_.running()) return NativeChatStatus::Reconnecting;
    if (ready_ && displayed_subscribed_ && connection_state_ && surface_.ready())
        return NativeChatStatus::Receiving;
    return NativeChatStatus::Connecting;
}
LRESULT CALLBACK NativeChatConnection::host_procedure(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    auto *self = reinterpret_cast<NativeChatConnection *>(data);
    if (message == self->open_message_ || message == self->query_message_) {
        // No pointer/string marshalling, authentication or remote commands.
        if (wparam || lparam || self->closed_) return 0;
        if (message == self->query_message_) return static_cast<LRESULT>(self->local_status());
        return self->open_dialog() ? 1 : 0;
    }
    if (message == self->hud_.config_changed_message_ && !self->closed_) {
        // The existing Apply external page command is the explicit switch.
        // Fence delivery and detach its document before HudWindow navigates;
        // even an in-flight first login frame must not reach the next page.
        if (wparam || lparam) return 0;
        self->auto_connect_pending_ = false;
        if (!self->signing_out_) self->end(
            L"외부 페이지 적용을 요청했습니다. 서버 로그아웃은 아닙니다. 복귀 가능 여부는 현재 표시 상태를 확인하세요.", true);
        const auto result = DefSubclassProc(window, message, wparam, lparam);
        self->show_display_status();
        return result;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, host_procedure, id);
        self->host_ = nullptr;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
bool NativeChatConnection::dispatch(MSG &message) noexcept
{
    if (!message.hwnd && message.message == WM_HOTKEY && message.wParam == kConnectHotkey) { open_dialog(); return true; }
    return dialog_ && IsWindowVisible(dialog_) && IsDialogMessageW(dialog_, &message);
}
void NativeChatConnection::notice(const wchar_t *value) noexcept { if (dialog_) SetDlgItemTextW(dialog_, kNotice, value); }
void NativeChatConnection::show_connection_state() noexcept
{
    if (!dialog_) return;
    show_display_status();
    try {
        const auto summary = connection_state_ ? connection_summary(*connection_state_)
            : std::wstring(unavailable_connection_text(local_status()));
        SetDlgItemTextW(dialog_, kSession, summary.c_str());
    } catch (...) { SetDlgItemTextW(dialog_, kSession, L"세션 상태를 확인하지 못했습니다."); }
}
void NativeChatConnection::show_display_status() noexcept
{
    if (!dialog_) return;
    const auto status = local_status();
    EnableWindow(GetDlgItem(dialog_, kManage), can_open_management(status));
    if (displayed_status_ == status) return;
    displayed_status_ = status;
    // A subscribed empty chat is not an acknowledged first text message.
    // This is local DOM status, not physical display or broadcast evidence.
    const wchar_t *label = status == NativeChatStatus::Receiving && first_text_rendered_
        ? L"자체 채팅 첫 텍스트 렌더링 확인 · 로컬 HUD 응답"
        : native_chat_status_text_ko(status);
    SetDlgItemTextW(dialog_, kDisplayStatus, label);
    EnableWindow(GetDlgItem(dialog_, kResume), can_request_native_chat_return(status));
    // The WinHTTP worker may retire AFTER clear_display() painted "Stopping".
    // Update the same session control on IdleResumable/ExternalPageResumable
    // transition, without restoring any stale socket counts.
    if (!connection_state_) SetDlgItemTextW(dialog_, kSession, unavailable_connection_text(status));
}
bool NativeChatConnection::open_dialog() noexcept
{
    if (closed_ || opening_dialog_) return false;
    opening_dialog_ = true;
    struct Reset { bool &flag; ~Reset() { flag = false; } } reset{opening_dialog_};
    try {
        if (hud_.shutting_down_ || hud_.system_suppressed() || hud_.capture_exclusion_failed_ ||
            hud_.capture_risk_ || !hud_.capture_exclusion_intact()) return false;
        if (dialog_) {
            DWORD affinity = 0;
            if (!GetWindowDisplayAffinity(dialog_, &affinity) || affinity != WDA_EXCLUDEFROMCAPTURE) return false;
            show_connection_state();
            ShowWindow(dialog_, SW_SHOWNORMAL); SetForegroundWindow(dialog_);
            return IsWindowVisible(dialog_) != FALSE;
        }
        WNDCLASSW klass{}; klass.lpfnWndProc = procedure; klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = kClass; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        dialog_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT | WS_EX_LAYERED, kClass,
            L"ChatView · 자체 채팅 연결 (개발 검증)", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 600, 624, nullptr, nullptr, klass.hInstance, this);
        if (!dialog_) return false;
        displayed_status_.reset();
        // GetWindowDisplayAffinity documents a layered-window prerequisite.
        // Keep this ordinary panel opaque and verify before its first showing,
        // so reopening can enforce the same guard even while it is hidden.
        DWORD affinity = 0;
        if (!SetLayeredWindowAttributes(dialog_, 0, 255, LWA_ALPHA) ||
            !SetWindowDisplayAffinity(dialog_, WDA_EXCLUDEFROMCAPTURE) ||
            !GetWindowDisplayAffinity(dialog_, &affinity) || affinity != WDA_EXCLUDEFROMCAPTURE) {
            DestroyWindow(dialog_); dialog_ = nullptr; return false;
        }
        const UINT dpi = GetDpiForWindow(dialog_);
        const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
        SetWindowPos(dialog_, nullptr, 0, 0, scale(600), scale(624), SWP_NOMOVE | SWP_NOZORDER);
        const auto add = [&](const wchar_t *kind, const wchar_t *caption, DWORD style,
                             int id, int x, int y, int width, int height) {
            HWND item = CreateWindowExW(std::wstring_view(kind) == L"EDIT" ? WS_EX_CLIENTEDGE : 0,
                kind, caption, WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(width), scale(height),
                dialog_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), klass.hInstance, nullptr);
            if (item) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return item;
        };
        add(L"STATIC", L"서비스 주소 (새 로그인용 · HTTPS, 경로 제외)", 0, 0, 20, 16, 550, 24);
        HWND origin = add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kOrigin, 20, 44, 550, 30);
        const auto role_text = std::wstring(L"현재 실행: ") + display_role_label(role_);
        add(L"STATIC", role_text.c_str(), 0, kRole, 20, 82, 550, 24);
        add(L"STATIC", L"", 0, kSession, 20, 110, 550, 72);
        add(L"BUTTON", L"개발용 로컬 서버 허용 (http://127.0.0.1만)", WS_TABSTOP | BS_AUTOCHECKBOX, kLocal, 20, 194, 550, 28);
        add(L"BUTTON", L"이 PC에서 연결 유지 · 다음 실행부터 자동 연결", WS_TABSTOP | BS_AUTOCHECKBOX, kRemember, 20, 228, 550, 28);
        add(L"BUTTON", L"로그인 / 연결", WS_TABSTOP | BS_DEFPUSHBUTTON, kConnect, 20, 272, 150, 32);
        add(L"BUTTON", L"연결 중지", WS_TABSTOP | BS_PUSHBUTTON, kDisconnect, 186, 272, 120, 32);
        add(L"BUTTON", L"로그아웃", WS_TABSTOP | BS_PUSHBUTTON, kForget, 322, 272, 150, 32);
        HWND resume = add(L"BUTTON", L"현재 승인으로 자체 채팅 복귀", WS_TABSTOP | BS_PUSHBUTTON,
            kResume, 20, 314, 300, 32);
        HWND manage = add(L"BUTTON", L"계정·시험 광고·활동 관리", WS_TABSTOP | BS_PUSHBUTTON,
            kManage, 336, 314, 234, 32);
        HWND management_scope = add(L"STATIC", L"브라우저의 로그인 계정으로 관리합니다. 열린 화면의 채널을 확인하세요.\n브라우저 화면은 HUD 캡처 보호 대상이 아닙니다.",
            0, kManagementScope, 20, 402, 550, 40);
        HWND display = add(L"STATIC", L"", 0, kDisplayStatus, 20, 354, 550, 38);
        add(L"STATIC", L"브라우저에서 채널과 요청 역할을 승인하세요. 기기등록이나 키 입력은 없습니다.\n공용 PC에서는 연결 유지를 선택하지 마세요.", 0, kNotice, 20, 452, 550, 80);
        // Always visible when this panel is open, independent of login, output
        // reports and transient notices. Local status never certifies video.
        HWND scope = add(L"STATIC", L"수신 영상의 HUD 제외: 미검증\n채팅 연결·OBS 활성 보고는 영상 검증이 아닙니다.",
            0, kVideoScope, 20, 538, 550, 40);
        if (!origin || !scope || !resume || !display || !manage || !management_scope) { DestroyWindow(dialog_); dialog_ = nullptr; return false; }
        SendMessageW(origin, EM_SETLIMITTEXT, 2048, 0);
        if (auto saved = load_connection()) {
            SetWindowTextW(origin, saved->origin.c_str());
            SendDlgItemMessageW(dialog_, kLocal, BM_SETCHECK, saved->developer_loopback ? BST_CHECKED : BST_UNCHECKED, 0);
            SendDlgItemMessageW(dialog_, kRemember, BM_SETCHECK, BST_CHECKED, 0);
            SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
        }
        show_connection_state();
        ShowWindow(dialog_, SW_SHOWNORMAL); SetForegroundWindow(dialog_); SetFocus(origin);
        return IsWindowVisible(dialog_) != FALSE;
    } catch (...) { if (dialog_) { DestroyWindow(dialog_); dialog_ = nullptr; } }
    return false;
}
LRESULT CALLBACK NativeChatConnection::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto *self = reinterpret_cast<NativeChatConnection *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<NativeChatConnection *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == kConnect || LOWORD(wparam) == IDOK) { self->connect(); return 0; }
        if (LOWORD(wparam) == kDisconnect) {
            if (self->signing_out_) { self->notice(L"로그아웃 중입니다."); return 0; }
            self->auto_connect_pending_ = false;
            self->end(L"연결을 종료했습니다. 서버 승인을 해제하려면 로그아웃을 선택하세요."); return 0;
        }
        if (LOWORD(wparam) == kResume) { self->resume_current(); return 0; }
        if (LOWORD(wparam) == kManage) { self->open_management(); return 0; }
        if (LOWORD(wparam) == kForget) { self->forget(); return 0; }
        if (LOWORD(wparam) == IDCANCEL) { SendMessageW(window, WM_CLOSE, 0, 0); return 0; }
    }
    if (message == WM_CLOSE) { ShowWindow(window, SW_HIDE); return 0; }
    if (message == WM_NCDESTROY) { SetWindowLongPtrW(window, GWLP_USERDATA, 0); self->dialog_ = nullptr; }
    return DefWindowProcW(window, message, wparam, lparam);
}
bool NativeChatConnection::open_surface() noexcept
{
    if (!surface_.open(hud_.webview_)) return false;
    first_text_rendered_ = false;
    ready_ = false; displayed_subscribed_ = false; awaiting_frame_ = 0; pending_.clear();
    loading_deadline_ = GetTickCount64() + 15000U;
    return true;
}
void NativeChatConnection::begin(std::wstring origin, std::wstring credential, bool local, DisplayAuthentication mode) noexcept
{
    auto_connect_pending_ = false;
    if (closed_ || !host_) {
        if (!credential.empty()) SecureZeroMemory(credential.data(), credential.size() * sizeof(wchar_t));
        notice(L"HUD 연결 제어를 준비하지 못했습니다."); return;
    }
    connection_state_.reset(); show_connection_state();
    std::wstring bound_origin;
    try { bound_origin = origin; }
    catch (...) {
        if (!credential.empty()) SecureZeroMemory(credential.data(), credential.size() * sizeof(wchar_t));
        notice(L"연결 주소를 준비하지 못했습니다."); return;
    }
    if (!client_.start(std::move(origin), std::move(credential), local, mode, role_)) { notice(L"주소 또는 연결 승인을 확인하세요."); return; }
    // start() has validated the origin. This is a non-secret navigation target,
    // never an extra approval or a value read back from editable controls.
    service_origin_ = std::move(bound_origin);
    awaiting_login_ = mode == DisplayAuthentication::Browser || mode == DisplayAuthentication::BrowserRemember;
    if (awaiting_login_) surface_.close();
    else if (!open_surface()) {
        end(L"자체 채팅 화면을 열지 못했습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요.");
        return;
    }
    active_ = true; reconnecting_ = false; remembered_ = mode == DisplayAuthentication::Saved || mode == DisplayAuthentication::Remember || mode == DisplayAuthentication::BrowserRemember;
    hud_.cancel_navigation_retry(); hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
    hud_.set_page_health(HudPageState::Loading, HudProvider::Chzzk);
    notice(L"채팅 연결 중입니다. 창을 닫아도 연결은 유지됩니다.");
}
void NativeChatConnection::connect() noexcept
{
    auto_connect_pending_ = false;
    try {
        if (client_.running() || signing_out_) { notice(L"연결 또는 종료 처리 중입니다. 중복 연결하지 않습니다."); return; }
        if (!hud_.webview_ready_ || hud_.system_suppressed() || hud_.shutting_down_ || hud_.capture_exclusion_failed_) {
            notice(L"HUD를 현재 사용할 수 없습니다. 캡처·잠금 보호는 우회하지 않습니다."); return;
        }
        if (client_.can_resume_current()) {
            notice(L"현재 승인이 남아 있습니다. 자체 채팅 복귀를 누르거나 로그아웃 후 새로 연결하세요."); return;
        }
        auto origin = text(dialog_, kOrigin, 2048);
        const bool local = SendDlgItemMessageW(dialog_, kLocal, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const bool remember = SendDlgItemMessageW(dialog_, kRemember, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (auto saved = load_connection()) {
            if (remember && saved->origin == origin && saved->developer_loopback == local) {
                begin(std::move(saved->origin), std::move(saved->credential), local, DisplayAuthentication::Saved); return;
            }
            SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
        }
        begin(std::move(origin), {}, local, remember ? DisplayAuthentication::BrowserRemember : DisplayAuthentication::Browser);
    } catch (...) { end(L"연결을 시작하지 못했습니다."); }
}
void NativeChatConnection::resume_current() noexcept
{
    auto_connect_pending_ = false;
    if (closed_ || !host_ || active_ || signing_out_ || client_.running()) {
        notice(L"연결 또는 종료 처리 중입니다. 중복 복귀하지 않습니다."); return;
    }
    if (!hud_.webview_ready_ || hud_.shutting_down_ || hud_.system_suppressed() ||
        hud_.capture_risk_ || hud_.capture_exclusion_failed_ || !hud_.capture_exclusion_intact()) {
        notice(L"현재 보호 상태에서는 자체 채팅에 복귀할 수 없습니다."); return;
    }
    if (!client_.resume_current()) {
        notice(L"복귀할 현재 승인이 없습니다. 로그아웃·철회 후에는 다시 로그인하세요."); return;
    }
    // Do not read the editable origin, remember checkbox or another DPAPI
    // approval here. The client renews its exact original context privately.
    connection_state_.reset(); awaiting_login_ = false; reconnecting_ = false;
    if (!open_surface()) {
        end(L"자체 채팅 화면을 열지 못했습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요.");
        return;
    }
    active_ = true; show_connection_state();
    hud_.cancel_navigation_retry(); hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
    hud_.set_page_health(HudPageState::Loading, HudProvider::Chzzk);
    notice(L"현재 승인으로 복귀 중입니다. 새 채팅 연결을 확인하고 표시합니다.");
}
bool NativeChatConnection::can_open_management(NativeChatStatus status) const noexcept
{
    return !closed_ && !opening_management_ && !signing_out_ && !service_origin_.empty() &&
        (status == NativeChatStatus::Receiving || can_request_native_chat_return(status));
}
void NativeChatConnection::open_management() noexcept
{
    if (!dialog_ || !IsWindowVisible(dialog_) || !can_open_management(local_status())) {
        notice(L"관리 화면을 열 현재 연결을 확인하지 못했습니다. 기존 연결과 보호 상태를 확인하세요."); return;
    }
    DWORD affinity = 0;
    if (!GetWindowDisplayAffinity(dialog_, &affinity) || affinity != WDA_EXCLUDEFROMCAPTURE) return;
    opening_management_ = true;
    struct Reset { bool &flag; ~Reset() { flag = false; } } reset{opening_management_};
    try {
        // Only the original validated service and one fixed public route.
        // Browser cookies decide the management account; no native credentials,
        // account identifiers, query strings or redirect targets are transferred.
        auto url = service_origin_;
        if (url.ends_with(L"/")) url.pop_back();
        url += L"/account";
        notice(browser_(dialog_, url.c_str())
            ? L"관리 화면 열기를 요청했습니다. 브라우저의 계정·채널을 확인하세요. 현재 표시·승인은 변경하지 않았습니다."
            : L"브라우저를 열지 못했습니다. 채팅 연결은 변경하지 않았습니다. 다시 시도하세요.");
    } catch (...) { notice(L"관리 화면을 열지 못했습니다. 채팅 연결은 변경하지 않았습니다."); }
}
void NativeChatConnection::forget() noexcept
{
    auto_connect_pending_ = false;
    if (signing_out_) { notice(L"로그아웃 중입니다."); return; }
    // The client owns both in-run and remembered approvals. It cancels receipt,
    // clears only matching storage and revokes on its worker, never on this UI.
    signing_out_ = client_.sign_out();
    clear_display();
    notice(signing_out_ ? L"로그아웃 중입니다. 서버 연결 승인을 해제하고 있습니다."
        : L"표시를 중지했습니다. 해제할 승인을 확인하지 못해 서버 로그아웃은 미확인입니다.");
}
void NativeChatConnection::clear_display(bool preserve_host) noexcept
{
    pending_.clear(); connection_state_.reset(); first_text_rendered_ = false;
    if (active_) {
        active_ = false; awaiting_login_ = false; ready_ = false; displayed_subscribed_ = false; awaiting_frame_ = 0U; surface_.close();
        if (!preserve_host) {
            hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
            hud_.set_page_health(HudPageState::ConnectionLost, HudProvider::Chzzk);
        }
    }
    show_connection_state();
}
void NativeChatConnection::end(const wchar_t *message, bool preserve_host) noexcept
{
    client_.stop(); clear_display(preserve_host); notice(message);
}
void NativeChatConnection::tick() noexcept
{
    try {
        show_display_status();
        if (connection_state_ && expire_reported_output(connection_state_->output, GetTickCount64()))
            show_connection_state();
        if (signing_out_) {
            if (client_.running()) return;
            signing_out_ = false;
            DisplayUpdate logout;
            const bool confirmed = client_.take(logout) && logout.status == DisplayStatus::SignedOut;
            show_display_status();
            notice(confirmed ? L"현재 연결을 로그아웃했습니다. 서버 승인도 해제했습니다."
                : L"로그아웃 미확인: 저장 정보 삭제 또는 서버 해제에 실패했습니다. 다시 로그아웃하거나 관리 화면에서 연결을 해제하세요.");
            return;
        }
        if (auto_connect_pending_ && !active_ && !client_.running() && hud_.webview_ready_ &&
            !hud_.system_suppressed() && !hud_.shutting_down_ && !hud_.capture_exclusion_failed_) {
            auto_connect_pending_ = false;
            if (auto saved = load_connection()) begin(std::move(saved->origin), std::move(saved->credential),
                saved->developer_loopback, DisplayAuthentication::Saved);
        }
        if (!active_) return;
        if (hud_.system_suppressed() || hud_.shutting_down_ || hud_.capture_exclusion_failed_) {
            auto_connect_pending_ = remembered_ && !hud_.shutting_down_ && !hud_.capture_exclusion_failed_;
            end(L"시스템 보호로 연결을 중지했습니다. 저장한 연결은 복귀 뒤 다시 연결합니다.", true);
            if (dialog_) { ShowWindow(dialog_, SW_HIDE); }
            return;
        }
        std::wstring login_url;
        if (client_.take_login_url(login_url)) {
            const auto suffix = login_url.substr(login_url.size() - 6U);
            std::wstring code = suffix;
            for (auto &c : code) if (c >= L'a' && c <= L'f') c = static_cast<wchar_t>(c - (L'a' - L'A'));
            const std::wstring message = L"브라우저에서 승인하세요. 확인 번호: " + code;
            notice(message.c_str());
            if (!browser_(dialog_, login_url.c_str())) { end(L"브라우저를 열지 못했습니다. 다시 로그인하세요."); return; }
        }
        DisplayUpdate update;
        if (client_.take(update)) {
            if (connection_state_ != update.connection) {
                connection_state_ = update.connection; show_connection_state();
            }
            if (update.status == DisplayStatus::RoleMismatch) {
                end(L"현재 실행 역할과 저장된 승인 역할이 다릅니다. 원래 실행 방식으로 돌아가거나 로그아웃 후 다시 승인하세요."); return;
            }
            if (update.status == DisplayStatus::Denied) {
                end(L"연결 승인이 해제·만료됐습니다. 다시 연결해 주세요."); return;
            }
            if (update.status == DisplayStatus::Ended || update.status == DisplayStatus::Failed) {
                end(L"로그인 또는 연결이 종료됐습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요. 승인 없이 로그인에 실패했다면 다시 로그인하세요."); return;
            }
            if (update.status == DisplayStatus::Reconnecting) {
                if (!reconnecting_ && !open_surface()) {
                    end(L"재연결 중 새 채팅 화면을 준비하지 못했습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요."); return;
                }
                reconnecting_ = true;
                notice(L"연결 복구 중입니다. 서버와 채널 승인이 준비되면 자동 재연결합니다.");
            }
            if (update.status == DisplayStatus::Receiving) {
                if (awaiting_login_) {
                    awaiting_login_ = false;
                    if (!open_surface()) {
                        end(L"승인 후 채팅 화면을 열지 못했습니다. 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요."); return;
                    }
                    if (dialog_ && IsWindowVisible(dialog_)) SetForegroundWindow(dialog_);
                }
                reconnecting_ = false; pending_ = std::move(update.envelope); pending_subscribed_ = update.subscribed;
                notice(update.subscribed ? (remembered_ ? L"채팅 연결을 유지하고 있습니다. 읽기 권한은 자동 갱신됩니다." : L"현재 실행 중 읽기 권한을 자동 갱신합니다. 다음 실행에는 다시 로그인합니다.") : L"서비스에 연결됐습니다. 채팅 구독을 기다립니다.");
            }
        }
        if (awaiting_login_) {
            hud_.cancel_navigation_retry(); hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
            return;
        }
        if (ready_ && !surface_.ready()) {
            end(L"채팅 표시 문서가 변경되어 출력을 중지했습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요.", true);
            return;
        }
        if (surface_.rejected_frames() != 0) {
            // A rejected first renderer frame must not destroy the existing
            // server approval or silently open a new login/browser flow.
            end(L"채팅 화면이 수신 데이터를 거부해 표시를 중지했습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요.");
            return;
        }
        if (!ready_ && GetTickCount64() >= loading_deadline_) {
            end(L"채팅 화면 준비 시간이 초과됐습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요.");
            return;
        }
        if (surface_.ready()) ready_ = true;
        hud_.cancel_navigation_retry(); hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
        if (awaiting_frame_ && surface_.rendered_frames() >= awaiting_frame_) {
            awaiting_frame_ = 0U; displayed_subscribed_ = in_flight_subscribed_;
            if (displayed_subscribed_ && !first_text_rendered_ && surface_.rendered_messages() > 0U) {
                first_text_rendered_ = true;
                displayed_status_.reset(); show_display_status();
            }
        }
        if (awaiting_frame_ && GetTickCount64() >= render_deadline_) {
            end(L"채팅 화면 응답이 중단됐습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요.");
            return;
        }
        if (ready_ && !awaiting_frame_ && !pending_.empty()) {
            if (!surface_.publish(pending_)) {
                end(L"채팅 화면에 메시지를 전달하지 못했습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요.");
                return;
            }
            awaiting_frame_ = surface_.rendered_frames() + 1U;
            in_flight_subscribed_ = pending_subscribed_; render_deadline_ = GetTickCount64() + 15000U; pending_.clear();
        }
        hud_.set_page_health(ready_ && displayed_subscribed_ ? HudPageState::Ready : HudPageState::Loading, HudProvider::Chzzk);
    } catch (...) {
        end(L"채팅 연결 처리 오류로 화면을 지웠습니다. 기존 승인이 유효하면 종료 후 '현재 승인으로 자체 채팅 복귀'를 선택하세요.");
    }
}
void NativeChatConnection::close() noexcept
{
    closed_ = true; service_origin_.clear();
    if (host_) { RemoveWindowSubclass(host_, host_procedure, kConnectHotkey); host_ = nullptr; }
    auto_connect_pending_ = false; client_.stop();
    connection_state_.reset();
    pending_.clear(); active_ = false; awaiting_login_ = false; surface_.close();
    if (hotkey_) { UnregisterHotKey(nullptr, kConnectHotkey); hotkey_ = false; }
    if (dialog_) { DestroyWindow(dialog_); dialog_ = nullptr; }
}
}