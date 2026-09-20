// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/native-chat-connection.hpp"
#include "hud/saved-connection.hpp"
#include "hud/hud-window.hpp"
#include <shellapi.h>
#include <string_view>
#include <utility>

namespace chatview {
namespace {
constexpr int kConnectHotkey = 0x4348;
constexpr int kOrigin = 101, kLocal = 103, kConnect = 104, kDisconnect = 105, kNotice = 106;
constexpr int kRemember = 107, kForget = 108;
constexpr wchar_t kClass[] = L"ChatView.NativeConnection";
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
NativeChatConnection::NativeChatConnection(HudWindow &hud, BrowserLauncher browser) noexcept
    : hud_(hud), browser_(browser ? browser : [](HWND window, const wchar_t *url) {
        return reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", url, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    })
{
    hotkey_ = RegisterHotKey(nullptr, kConnectHotkey, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'C') != FALSE;
    if (!hotkey_) OutputDebugStringW(L"[ChatView HUD] Native chat connection hotkey unavailable\n");
}
NativeChatConnection::~NativeChatConnection() { close(); }
bool NativeChatConnection::dispatch(MSG &message) noexcept
{
    if (!message.hwnd && message.message == WM_HOTKEY && message.wParam == kConnectHotkey) { open_dialog(); return true; }
    return dialog_ && IsWindowVisible(dialog_) && IsDialogMessageW(dialog_, &message);
}
void NativeChatConnection::notice(const wchar_t *value) noexcept { if (dialog_) SetDlgItemTextW(dialog_, kNotice, value); }
void NativeChatConnection::open_dialog() noexcept
{
    try {
        if (hud_.shutting_down_ || hud_.system_suppressed() || hud_.capture_exclusion_failed_) return;
        if (dialog_) { ShowWindow(dialog_, SW_SHOWNORMAL); SetForegroundWindow(dialog_); return; }
        WNDCLASSW klass{}; klass.lpfnWndProc = procedure; klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = kClass; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        dialog_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kClass,
            L"ChatView · 자체 채팅 연결 (개발 검증)", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 600, 380, nullptr, nullptr, klass.hInstance, this);
        if (!dialog_) return;
        if (!SetWindowDisplayAffinity(dialog_, WDA_EXCLUDEFROMCAPTURE)) { DestroyWindow(dialog_); dialog_ = nullptr; return; }
        const UINT dpi = GetDpiForWindow(dialog_);
        const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
        SetWindowPos(dialog_, nullptr, 0, 0, scale(600), scale(380), SWP_NOMOVE | SWP_NOZORDER);
        const auto add = [&](const wchar_t *kind, const wchar_t *caption, DWORD style,
                             int id, int x, int y, int width, int height) {
            HWND item = CreateWindowExW(std::wstring_view(kind) == L"EDIT" ? WS_EX_CLIENTEDGE : 0,
                kind, caption, WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(width), scale(height),
                dialog_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), klass.hInstance, nullptr);
            if (item) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return item;
        };
        add(L"STATIC", L"서비스 주소 (HTTPS, 경로 제외)", 0, 0, 20, 16, 550, 24);
        HWND origin = add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kOrigin, 20, 44, 550, 30);
        add(L"STATIC", L"브라우저에서 치지직 연결을 승인하면 채팅이 표시됩니다.", 0, 0, 20, 86, 550, 28);
        add(L"BUTTON", L"개발용 로컬 서버 허용 (http://127.0.0.1만)", WS_TABSTOP | BS_AUTOCHECKBOX, kLocal, 20, 124, 550, 28);
        add(L"BUTTON", L"이 PC에서 연결 유지 · 다음 실행부터 자동 연결", WS_TABSTOP | BS_AUTOCHECKBOX, kRemember, 20, 158, 550, 28);
        add(L"BUTTON", L"로그인 / 연결", WS_TABSTOP | BS_DEFPUSHBUTTON, kConnect, 20, 202, 150, 32);
        add(L"BUTTON", L"연결 중지", WS_TABSTOP | BS_PUSHBUTTON, kDisconnect, 186, 202, 120, 32);
        add(L"BUTTON", L"로그아웃", WS_TABSTOP | BS_PUSHBUTTON, kForget, 322, 202, 150, 32);
        add(L"STATIC", L"앱에서 시작한 요청만 승인하세요. 키를 복사할 필요가 없습니다.\n공용 PC에서는 연결 유지를 선택하지 마세요.", 0, kNotice, 20, 254, 550, 76);
        if (!origin) { DestroyWindow(dialog_); dialog_ = nullptr; return; }
        SendMessageW(origin, EM_SETLIMITTEXT, 2048, 0);
        if (auto saved = load_connection()) {
            SetWindowTextW(origin, saved->origin.c_str());
            SendDlgItemMessageW(dialog_, kLocal, BM_SETCHECK, saved->developer_loopback ? BST_CHECKED : BST_UNCHECKED, 0);
            SendDlgItemMessageW(dialog_, kRemember, BM_SETCHECK, BST_CHECKED, 0);
            SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
        }
        ShowWindow(dialog_, SW_SHOWNORMAL); SetForegroundWindow(dialog_); SetFocus(origin);
    } catch (...) { if (dialog_) { DestroyWindow(dialog_); dialog_ = nullptr; } }
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
        if (LOWORD(wparam) == kDisconnect) { self->auto_connect_pending_ = false; self->end(L"연결을 종료했습니다. 저장한 연결은 키 없이 다시 연결할 수 있습니다."); return 0; }
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
    ready_ = false; displayed_subscribed_ = false; awaiting_frame_ = 0; pending_.clear();
    loading_deadline_ = GetTickCount64() + 15000U;
    return true;
}
void NativeChatConnection::begin(std::wstring origin, std::wstring credential, bool local, DisplayAuthentication mode) noexcept
{
    auto_connect_pending_ = false;
    if (!client_.start(std::move(origin), std::move(credential), local, mode)) { notice(L"주소 또는 연결 승인을 확인하세요."); return; }
    awaiting_login_ = mode == DisplayAuthentication::Browser || mode == DisplayAuthentication::BrowserRemember;
    if (awaiting_login_) surface_.close();
    else if (!open_surface()) { client_.stop(); notice(L"자체 채팅 화면을 열지 못했습니다."); return; }
    active_ = true; reconnecting_ = false; remembered_ = mode == DisplayAuthentication::Saved || mode == DisplayAuthentication::Remember || mode == DisplayAuthentication::BrowserRemember;
    hud_.cancel_navigation_retry(); hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
    hud_.set_page_health(HudPageState::Loading, HudProvider::Chzzk);
    notice(L"채팅 연결 중입니다. 창을 닫아도 연결은 유지됩니다.");
}
void NativeChatConnection::connect() noexcept
{
    auto_connect_pending_ = false;
    try {
        if (client_.running() || forget_pending_ || signing_out_) { notice(L"연결 또는 종료 처리 중입니다. 중복 연결하지 않습니다."); return; }
        if (!hud_.webview_ready_ || hud_.system_suppressed() || hud_.shutting_down_ || hud_.capture_exclusion_failed_) {
            notice(L"HUD를 현재 사용할 수 없습니다. 캡처·잠금 보호는 우회하지 않습니다."); return;
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
void NativeChatConnection::forget() noexcept
{
    auto_connect_pending_ = false;
    // Wait for cancellation before deleting: a pending exchange must not
    // write a saved login after the user has requested logout.
    end(L"로그아웃 중입니다.");
    forget_pending_ = true;
}
void NativeChatConnection::end(const wchar_t *message, bool preserve_host) noexcept
{
    client_.stop(); pending_.clear();
    if (active_) {
        active_ = false; awaiting_login_ = false; ready_ = false; displayed_subscribed_ = false; awaiting_frame_ = 0U; surface_.close();
        if (!preserve_host) {
            hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
            hud_.set_page_health(HudPageState::ConnectionLost, HudProvider::Chzzk);
        }
    }
    notice(message);
}
void NativeChatConnection::tick() noexcept
{
    try {
        if (forget_pending_) {
            if (client_.running()) return;
            forget_pending_ = false;
            auto saved = load_connection();
            if (!forget_connection()) {
                notice(L"이 PC의 연결 정보 삭제에 실패했습니다. 다시 로그아웃해 주세요.");
                if (saved) SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
                return;
            }
            if (saved) {
                signing_out_ = client_.start(std::move(saved->origin), std::move(saved->credential),
                    saved->developer_loopback, DisplayAuthentication::SignOut);
                if (!signing_out_) notice(L"로컬 연결은 삭제했습니다. 서버 해제는 확인하지 못했습니다.");
            } else notice(L"이 PC에서 로그아웃했습니다.");
            return;
        }
        if (signing_out_) {
            DisplayUpdate logout;
            if (client_.running()) return;
            signing_out_ = false;
            const bool confirmed = client_.take(logout) && logout.status == DisplayStatus::SignedOut;
            notice(confirmed ? L"로그아웃했습니다. 저장 정보와 서버 연결 승인을 해제했습니다."
                : L"로컬 연결은 삭제했습니다. 서버 해제는 확인하지 못했습니다. 관리 화면에서도 연결을 해제할 수 있습니다.");
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
            if (update.status == DisplayStatus::Denied) {
                end(L"연결 승인이 해제·만료됐습니다. 다시 연결해 주세요."); return;
            }
            if (update.status == DisplayStatus::Ended || update.status == DisplayStatus::Failed) {
                end(L"로그인 또는 연결이 종료됐습니다. 로그인 / 연결을 다시 선택하세요."); return;
            }
            if (update.status == DisplayStatus::Reconnecting) {
                // Clear the old document, not merely the connection label. A
                // single cleared surface is retained throughout retry backoff.
                if (!reconnecting_ && !open_surface()) { end(L"연결 손실 뒤 화면을 지우지 못했습니다."); return; }
                reconnecting_ = true;
                notice(L"연결 복구 중입니다. 서버와 채널 승인이 준비되면 자동 재연결합니다.");
            }
            if (update.status == DisplayStatus::Receiving) {
                if (awaiting_login_) {
                    awaiting_login_ = false;
                    if (!open_surface()) { end(L"승인 후 채팅 화면을 열지 못했습니다."); return; }
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
        if (ready_ && !surface_.ready()) { end(L"표시 문서가 변경되어 기존 연결을 종료했습니다.", true); return; }
        if (surface_.rejected_frames() != 0 || (!ready_ && GetTickCount64() >= loading_deadline_)) {
            end(L"화면 또는 수신 데이터가 유효하지 않아 연결을 종료했습니다."); return;
        }
        if (surface_.ready()) ready_ = true;
        hud_.cancel_navigation_retry(); hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
        if (awaiting_frame_ && surface_.rendered_frames() >= awaiting_frame_) { awaiting_frame_ = 0U; displayed_subscribed_ = in_flight_subscribed_; }
        if (awaiting_frame_ && GetTickCount64() >= render_deadline_) { end(L"화면 응답이 중단되어 연결을 종료했습니다."); return; }
        if (ready_ && !awaiting_frame_ && !pending_.empty()) {
            if (!surface_.publish(pending_)) { end(L"화면 전달이 중단됐습니다. 다시 연결하세요."); return; }
            awaiting_frame_ = surface_.rendered_frames() + 1U;
            in_flight_subscribed_ = pending_subscribed_; render_deadline_ = GetTickCount64() + 15000U; pending_.clear();
        }
        hud_.set_page_health(ready_ && displayed_subscribed_ ? HudPageState::Ready : HudPageState::Loading, HudProvider::Chzzk);
    } catch (...) { end(L"연결 처리 오류로 채팅을 지웠습니다."); }
}
void NativeChatConnection::close() noexcept
{
    auto_connect_pending_ = false; client_.stop();
    if (forget_pending_) (void)forget_connection();
    pending_.clear(); active_ = false; awaiting_login_ = false; surface_.close();
    if (hotkey_) { UnregisterHotKey(nullptr, kConnectHotkey); hotkey_ = false; }
    if (dialog_) { DestroyWindow(dialog_); dialog_ = nullptr; }
}
}
