// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/native-chat-connection.hpp"
#include "hud/hud-window.hpp"
#include <array>
#include <string_view>
#include <utility>

namespace chatview {
namespace {
constexpr int kConnectHotkey = 0x4348;
constexpr int kOrigin = 101, kTicket = 102, kLocal = 103, kConnect = 104, kDisconnect = 105, kNotice = 106;
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
NativeChatConnection::NativeChatConnection(HudWindow &hud) noexcept : hud_(hud)
{
    hotkey_ = RegisterHotKey(nullptr, kConnectHotkey,
        MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'C') != FALSE;
    if (!hotkey_) OutputDebugStringW(L"[ChatView HUD] Native chat connection hotkey unavailable\n");
}
NativeChatConnection::~NativeChatConnection() { close(); }
bool NativeChatConnection::dispatch(MSG &message) noexcept
{
    if (!message.hwnd && message.message == WM_HOTKEY && message.wParam == kConnectHotkey) {
        open_dialog(); return true;
    }
    return dialog_ && IsWindowVisible(dialog_) && IsDialogMessageW(dialog_, &message);
}
void NativeChatConnection::notice(const wchar_t *value) noexcept
{
    if (dialog_) SetDlgItemTextW(dialog_, kNotice, value);
}
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
        if (!SetWindowDisplayAffinity(dialog_, WDA_EXCLUDEFROMCAPTURE)) {
            DestroyWindow(dialog_); dialog_ = nullptr; return;
        }
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
        add(L"STATIC", L"승인 화면에서 발급한 1회용 표시 키 · 계정 비밀번호가 아닙니다", 0, 0, 20, 86, 550, 24);
        HWND ticket = add(L"EDIT", L"", WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, kTicket, 20, 114, 550, 30);
        add(L"BUTTON", L"개발용 로컬 서버 허용 (http://127.0.0.1만)", WS_TABSTOP | BS_AUTOCHECKBOX, kLocal, 20, 156, 550, 28);
        add(L"BUTTON", L"연결", WS_TABSTOP | BS_DEFPUSHBUTTON, kConnect, 20, 198, 120, 32);
        add(L"BUTTON", L"연결 종료", WS_TABSTOP | BS_PUSHBUTTON, kDisconnect, 156, 198, 120, 32);
        add(L"STATIC", L"Ctrl+Alt+Shift+C로 열기. 키는 저장하지 않습니다.\n현재는 짧은 표시 권한의 개발 검증 단계입니다.", 0, kNotice, 20, 246, 550, 70);
        if (!origin || !ticket) { DestroyWindow(dialog_); dialog_ = nullptr; return; }
        SendMessageW(origin, EM_SETLIMITTEXT, 2048, 0); SendMessageW(ticket, EM_SETLIMITTEXT, 64, 0);
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
        if (LOWORD(wparam) == kDisconnect) { self->end(L"연결을 종료했습니다. 다시 연결하려면 새 표시 키를 발급하세요."); return 0; }
        if (LOWORD(wparam) == IDCANCEL) { SendMessageW(window, WM_CLOSE, 0, 0); return 0; }
    }
    if (message == WM_CLOSE) { SetDlgItemTextW(window, kTicket, L""); ShowWindow(window, SW_HIDE); return 0; }
    if (message == WM_NCDESTROY) { SetWindowLongPtrW(window, GWLP_USERDATA, 0); self->dialog_ = nullptr; }
    return DefWindowProcW(window, message, wparam, lparam);
}
void NativeChatConnection::connect() noexcept
{
    try {
        if (client_.running()) { notice(L"연결 실행 또는 종료 처리 중입니다. 중복 연결하지 않습니다."); return; }
        if (!hud_.webview_ready_ || hud_.system_suppressed() || hud_.shutting_down_ || hud_.capture_exclusion_failed_) {
            notice(L"HUD를 현재 사용할 수 없습니다. 캡처·잠금 보호는 우회하지 않습니다."); return;
        }
        std::wstring origin = text(dialog_, kOrigin, 2048);
        std::wstring ticket = text(dialog_, kTicket, 64);
        const bool local = SendDlgItemMessageW(dialog_, kLocal, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SetDlgItemTextW(dialog_, kTicket, L"");
        if (!client_.start(std::move(origin), std::move(ticket), local)) { notice(L"주소 또는 1회용 표시 키를 확인하세요."); return; }
        if (!surface_.open(hud_.webview_)) { client_.stop(); notice(L"자체 채팅 화면을 열지 못했습니다."); return; }
        active_ = true; ready_ = false; displayed_subscribed_ = false; awaiting_frame_ = 0U; pending_.clear(); loading_deadline_ = GetTickCount64() + 15000U;
        hud_.cancel_navigation_retry(); hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
        hud_.set_page_health(HudPageState::Loading, HudProvider::Chzzk);
        notice(L"읽기 전용 연결 중입니다. 창을 닫아도 연결은 유지됩니다.");
    } catch (...) { end(L"연결을 시작하지 못했습니다. 새 표시 키로 다시 시도하세요."); }
}
void NativeChatConnection::end(const wchar_t *message, bool preserve_host) noexcept
{
    client_.stop(); pending_.clear();
    if (active_) {
        active_ = false; ready_ = false; displayed_subscribed_ = false; awaiting_frame_ = 0U; surface_.close();
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
        if (!active_) return;
        if (hud_.system_suppressed() || hud_.shutting_down_ || hud_.capture_exclusion_failed_) {
            end(L"시스템 보호 상태로 연결을 종료했습니다. 새 승인이 필요합니다.", true);
            if (dialog_) { SetDlgItemTextW(dialog_, kTicket, L""); ShowWindow(dialog_, SW_HIDE); }
            return;
        }
        if (ready_ && !surface_.ready()) {
            // A settings change or new host document owns its own health/recovery.
            end(L"표시 문서가 변경되어 기존 연결을 종료했습니다.", true); return;
        }
        if (surface_.rejected_frames() != 0 ||
            (!ready_ && GetTickCount64() >= loading_deadline_)) {
            end(L"화면 또는 수신 데이터가 유효하지 않아 연결을 종료했습니다."); return;
        }
        if (surface_.ready()) ready_ = true;
        DisplayUpdate update;
        if (client_.take(update)) {
            if (update.status == DisplayStatus::Ended || update.status == DisplayStatus::Failed) {
                end(L"연결이 종료됐거나 권한이 만료됐습니다. 새 표시 키로 다시 연결하세요."); return;
            }
            if (update.status == DisplayStatus::Receiving) {
                pending_ = std::move(update.envelope);
                pending_subscribed_ = update.subscribed;
                notice(update.subscribed ? L"채팅 구독 상태를 수신했습니다. 개인 HUD에만 표시합니다." : L"서비스에 연결됐습니다. 채팅 구독을 기다립니다.");
            }
        }
        // External-page DOM heuristics do not supervise a first-party feed.
        hud_.cancel_navigation_retry(); hud_.page_connection_recovery_.reset(); hud_.page_health_watchdog_.disarm();
        if (awaiting_frame_ && surface_.rendered_frames() >= awaiting_frame_) {
            awaiting_frame_ = 0U; displayed_subscribed_ = in_flight_subscribed_;
        }
        if (awaiting_frame_ && GetTickCount64() >= render_deadline_) {
            end(L"화면 응답이 중단되어 연결을 종료했습니다."); return;
        }
        // One WebView message in flight plus one coalesced newest snapshot.
        if (ready_ && !awaiting_frame_ && !pending_.empty()) {
            if (!surface_.publish(pending_)) { end(L"화면 전달이 중단됐습니다. 다시 연결하세요."); return; }
            awaiting_frame_ = surface_.rendered_frames() + 1U;
            in_flight_subscribed_ = pending_subscribed_; render_deadline_ = GetTickCount64() + 15000U;
            pending_.clear();
        }
        hud_.set_page_health(ready_ && displayed_subscribed_
            ? HudPageState::Ready : HudPageState::Loading, HudProvider::Chzzk);
    } catch (...) { end(L"연결 처리 오류로 채팅을 지웠습니다."); }
}
void NativeChatConnection::close() noexcept
{
    client_.stop(); pending_.clear(); active_ = false; surface_.close();
    if (hotkey_) { UnregisterHotKey(nullptr, kConnectHotkey); hotkey_ = false; }
    if (dialog_) { DestroyWindow(dialog_); dialog_ = nullptr; }
}
} // namespace chatview
