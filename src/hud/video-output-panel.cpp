// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/video-output-panel.hpp"
#include "hud/hud-window.hpp"
#include "hud/video-layout.hpp"
#include <dwmapi.h>
#include <wtsapi32.h>
#include <dbt.h>
#include <cwchar>
#include <stdexcept>

namespace chatview {
namespace {
constexpr int kVideoHotkey = 0x4356;
constexpr int kSource = 201, kMonitor = 202, kRefresh = 203, kStart = 204, kStop = 205, kRelease = 206, kNotice = 207;
constexpr wchar_t kPanelClass[] = L"ChatView.VideoSelection";
constexpr wchar_t kOutputClass[] = L"ChatView.WindowVideoOutput";
VideoRect rectangle(RECT r) noexcept { return {r.left, r.top, r.right, r.bottom}; }
bool equal(RECT a, RECT b) noexcept { return EqualRect(&a, &b) != FALSE; }
// An active GDI source with multiple target paths is a clone. Fail closed on
// topology/advanced-color query errors rather than guessing a safe output.
bool extended_sdr(const wchar_t *output, const wchar_t *source)
{
    UINT32 path_count = 0, mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS ||
        !path_count || path_count > 64 || mode_count > 256) return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr) != ERROR_SUCCESS) return false;
    paths.resize(path_count);
    unsigned output_paths = 0, source_paths = 0;
    for (const auto &path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
        name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        name.header.size = sizeof(name); name.header.adapterId = path.sourceInfo.adapterId; name.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS) return false;
        const bool destination = std::wcscmp(name.viewGdiDeviceName, output) == 0;
        const bool origin = std::wcscmp(name.viewGdiDeviceName, source) == 0;
        if (!destination && !origin) continue;
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
        color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        color.header.size = sizeof(color); color.header.adapterId = path.targetInfo.adapterId; color.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&color.header) != ERROR_SUCCESS || color.advancedColorEnabled) return false;
        if (destination) ++output_paths;
        if (origin) ++source_paths;
    }
    return output_paths == 1 && source_paths == 1 && std::wcscmp(source, output) != 0;
}
}
VideoOutputPanel::VideoOutputPanel(HudWindow &hud, bool companion) noexcept : hud_(hud), enabled_(companion)
{
    if (enabled_) hotkey_ = RegisterHotKey(nullptr, kVideoHotkey, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'V') != FALSE;
}
VideoOutputPanel::~VideoOutputPanel() { close(); }
DWORD VideoOutputPanel::wait_timeout() const noexcept { return enabled_ && (output_ || capture_.running()) ? 50U : INFINITE; }
bool VideoOutputPanel::permitted() const noexcept
{
    return enabled_ && !closing_ && !session_blocked_ && !suspended_ && hud_.webview_ready_ && !hud_.shutting_down_ && !hud_.system_suppressed() &&
        !hud_.capture_exclusion_failed_ && hud_.window_ && IsWindowVisible(hud_.window_);
}
bool VideoOutputPanel::dispatch(MSG &message) noexcept
{
    if (enabled_ && !message.hwnd && message.message == WM_HOTKEY && message.wParam == kVideoHotkey) { open(); return true; }
    return panel_ && IsWindowVisible(panel_) && IsDialogMessageW(panel_, &message);
}
void VideoOutputPanel::notice(const wchar_t *message) noexcept { if (panel_) SetDlgItemTextW(panel_, kNotice, message); }
void VideoOutputPanel::open() noexcept
{
    try {
        if (!permitted()) return;
        if (panel_) { ShowWindow(panel_, SW_SHOWNORMAL); SetForegroundWindow(panel_); return; }
        WNDCLASSW klass{}; klass.hInstance = GetModuleHandleW(nullptr); klass.lpfnWndProc = procedure;
        klass.hCursor = LoadCursorW(nullptr, IDC_ARROW); klass.lpszClassName = kPanelClass;
        klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        klass.lpszClassName = kOutputClass; klass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        panel_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME, kPanelClass,
            L"ChatView · 게임 창 별도 출력 (실험)", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 660, 420, nullptr, nullptr, klass.hInstance, this);
        if (!panel_) return;
        if (!SetWindowDisplayAffinity(panel_, WDA_EXCLUDEFROMCAPTURE)) { DestroyWindow(panel_); panel_ = nullptr; return; }
        notifications_ = WTSRegisterSessionNotification(panel_, NOTIFY_FOR_THIS_SESSION) != FALSE;
        if (!notifications_) { DestroyWindow(panel_); panel_ = nullptr; return; }
        const auto dpi = GetDpiForWindow(panel_);
        const auto scale = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
        SetWindowPos(panel_, nullptr, 0, 0, scale(660), scale(420), SWP_NOMOVE | SWP_NOZORDER);
        const auto add = [&](const wchar_t *kind, const wchar_t *caption, DWORD style, int id, int x, int y, int w, int h) {
            HWND item = CreateWindowExW(0, kind, caption, WS_CHILD | WS_VISIBLE | style,
                scale(x), scale(y), scale(w), scale(h), panel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), klass.hInstance, nullptr);
            if (!item) throw std::runtime_error("Video controls unavailable");
            SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        };
        add(L"STATIC", L"게임 창만 선택합니다. 화면 전체·챗뷰 자체 창은 캡처하지 않습니다.", 0, 0, 20, 18, 610, 24);
        add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kSource, 20, 48, 610, 180);
        add(L"STATIC", L"별도 확장 출력 (캡처카드 입력) · 복제/주 화면/HDR는 허용하지 않습니다.", 0, 0, 20, 94, 610, 24);
        add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kMonitor, 20, 124, 610, 150);
        add(L"BUTTON", L"목록 새로고침", BS_PUSHBUTTON | WS_TABSTOP, kRefresh, 20, 177, 140, 32);
        add(L"BUTTON", L"선택 창 출력", BS_PUSHBUTTON | WS_TABSTOP, kStart, 175, 177, 140, 32);
        add(L"BUTTON", L"중지 · 검은 화면", BS_PUSHBUTTON | WS_TABSTOP, kStop, 330, 177, 145, 32);
        add(L"BUTTON", L"출력 창 닫기", BS_PUSHBUTTON | WS_TABSTOP, kRelease, 490, 177, 140, 32);
        add(L"STATIC", L"영상만 전달합니다. 오디오·인코딩·네트워크 송출은 없습니다.\n로컬 HUD는 유지합니다. 실제 캡처카드/송출 PC 녹화 검증 전에는 방송에 사용하지 마세요.\n중지는 출력을 검게 유지합니다. 출력 창을 닫거나 앱을 종료하면 바탕화면이 다시 보일 수 있습니다.", 0, 0, 20, 226, 610, 78);
        add(L"STATIC", L"대기 중 · 투컴 영상 미검증", 0, kNotice, 20, 317, 610, 52);
        refresh(); ShowWindow(panel_, SW_SHOWNORMAL); SetForegroundWindow(panel_);
    } catch (...) { if (panel_) DestroyWindow(panel_);
    panel_ = nullptr; }
}
void VideoOutputPanel::refresh()
{
    if (!permitted() || !notifications_) return;
    if (capture_.running() || requested_) { notice(L"출력을 중지한 뒤 목록을 갱신하세요."); return; }
    invalidate_choices();
    // Never let allocation exceptions cross Win32 callbacks.
    bool failed = false;
    struct Context { VideoOutputPanel *self; bool *failed; } context{this, &failed};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto &c = *reinterpret_cast<Context *>(parameter);
        try {
            if (c.self->sources_.size() >= 256) return FALSE;
            DWORD pid = 0, cloaked = 0; const DWORD thread = GetWindowThreadProcessId(window, &pid);
            if (!pid || !thread || pid == GetCurrentProcessId() || !IsWindowVisible(window) || IsIconic(window) ||
                GetAncestor(window, GA_ROOT) != window || window == GetDesktopWindow() || window == GetShellWindow() ||
                FAILED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked) return TRUE;
            wchar_t title[257]{}; if (!GetWindowTextW(window, title, 257)) return TRUE;
            c.self->sources_.push_back({window, pid, thread, title}); return TRUE;
        } catch (...) { *c.failed = true; return FALSE; }
    }, reinterpret_cast<LPARAM>(&context));
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM parameter) -> BOOL {
        auto &c = *reinterpret_cast<Context *>(parameter);
        try {
            if (c.self->monitors_.size() >= 16) return FALSE;
            MONITORINFOEXW info{}; info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) && !(info.dwFlags & MONITORINFOF_PRIMARY)) c.self->monitors_.push_back({monitor, info});
            return TRUE;
        } catch (...) { *c.failed = true; return FALSE; }
    }, reinterpret_cast<LPARAM>(&context));
    if (failed) throw std::runtime_error("Video selection unavailable");
    // A failed control insertion must never shift displayed labels away from
    // the validated window/monitor entries subsequently selected by index.
    const auto append = [&](int control, const wchar_t *label, size_t index) {
        if (SendDlgItemMessageW(panel_, control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label)) != static_cast<LRESULT>(index))
            throw std::runtime_error("Video selection insertion failed");
    };
    try {
        for (size_t i = 0; i < sources_.size(); ++i) append(kSource, sources_[i].title.c_str(), i);
        for (size_t i = 0; i < monitors_.size(); ++i) {
            const auto &monitor = monitors_[i];
            const auto &r = monitor.info.rcMonitor;
            const std::wstring label = std::wstring(monitor.info.szDevice) + L" · " +
                std::to_wstring(r.right - r.left) + L"×" + std::to_wstring(r.bottom - r.top);
            append(kMonitor, label.c_str(), i);
        }
    } catch (...) {
        sources_.clear(); monitors_.clear();
        SendDlgItemMessageW(panel_, kSource, CB_RESETCONTENT, 0, 0);
        SendDlgItemMessageW(panel_, kMonitor, CB_RESETCONTENT, 0, 0);
        throw;
    }
    // No preselected target; starting always requires the user's two choices.
    notice(monitors_.empty() ? L"별도 확장 화면이 없습니다. 복제 화면을 출력 대상으로 사용하지 않습니다." : L"게임 창과 별도 출력 화면을 선택하세요. 자동 선택/재시작하지 않습니다.");
}
bool VideoOutputPanel::topology(bool inspect_paths) const noexcept
{
    try {
        if (!permitted() || !source_ || !monitor_ || MonitorFromWindow(source_, MONITOR_DEFAULTTONULL) != source_monitor_) return false;
        DWORD pid = 0; if (GetWindowThreadProcessId(source_, &pid) != source_thread_ || !pid || pid != source_process_) return false;
        RECT source{}, hud{}, panel{};
        MONITORINFOEXW output{}, input{}; output.cbSize = sizeof(output); input.cbSize = sizeof(input);
        if (!GetWindowRect(source_, &source) || !GetWindowRect(hud_.window_, &hud) || !GetWindowRect(panel_, &panel) ||
            !GetMonitorInfoW(monitor_, &output) || !GetMonitorInfoW(MonitorFromWindow(source_, MONITOR_DEFAULTTONULL), &input) ||
            !equal(output.rcMonitor, output_bounds_) || output_device_ != output.szDevice || video_overlap(rectangle(panel), rectangle(output.rcMonitor))) return false;
        return video_size(output.rcMonitor.right - output.rcMonitor.left, output.rcMonitor.bottom - output.rcMonitor.top) &&
            separate_video_output(rectangle(output.rcMonitor), rectangle(source), rectangle(hud),
                (!inspect_paths || extended_sdr(output.szDevice, input.szDevice)), (output.dwFlags & MONITORINFOF_PRIMARY) != 0);
    } catch (...) { return false; }
}
void VideoOutputPanel::start()
{
    if (capture_.running() || requested_ || releasing_ || confirming_ || !notifications_ || !permitted()) { notice(L"현재 출력을 시작할 수 없습니다. 중지 상태와 HUD를 확인하세요."); return; }
    const LRESULT source = SendDlgItemMessageW(panel_, kSource, CB_GETCURSEL, 0, 0);
    const LRESULT monitor = SendDlgItemMessageW(panel_, kMonitor, CB_GETCURSEL, 0, 0);
    if (source < 0 || monitor < 0 || static_cast<size_t>(source) >= sources_.size() || static_cast<size_t>(monitor) >= monitors_.size()) {
        notice(L"게임 창과 별도 확장 출력 화면을 직접 선택하세요."); return;
    }
    const auto chosen = sources_[static_cast<size_t>(source)];
    const auto destination = monitors_[static_cast<size_t>(monitor)];
    // Never destroy an existing black output merely to start or retarget. A
    // different output needs its own explicit release, including the warning.
    if (output_ && (monitor_ != destination.handle || !equal(output_bounds_, destination.info.rcMonitor) ||
        output_device_ != destination.info.szDevice || !output_intact())) {
        notice(L"기존 검은 출력창을 유지합니다. 다른 출력으로 바꾸려면 수신 장면을 중지하고 '출력 창 닫기'부터 선택하세요."); return;
    }
    source_ = chosen.window; source_process_ = chosen.process; source_thread_ = chosen.thread;
    source_monitor_ = MonitorFromWindow(source_, MONITOR_DEFAULTTONULL);
    monitor_ = destination.handle; output_bounds_ = destination.info.rcMonitor; output_device_ = destination.info.szDevice;
    if (!topology(true)) { notice(L"분리된 SDR 확장 출력을 확인하지 못했습니다. 게임 창/HUD/설정창은 출력 화면에 둘 수 없습니다."); return; }
    const auto epoch = selection_epoch_;
    confirming_ = true;
    const int answer = MessageBoxW(panel_, L"선택한 게임 창의 영상만 별도 확장 화면에 출력합니다.\n\n캡처카드는 그 확장 화면 출력을 받아야 합니다.\n화면 복제·HDR·오디오 전달은 지원하지 않습니다.\n개인 HUD는 게임 화면에 유지하세요. 실제 투컴 영상은 아직 미검증입니다.\n\n선택한 창과 출력 화면으로 시작할까요?",
        L"ChatView · 별도 영상 출력 확인", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    confirming_ = false;
    // The modal dialog pumps messages: a lock/unlock or topology change during
    // consent invalidates that consent even if conditions now look normal.
    if (answer != IDYES || epoch != selection_epoch_ || !topology(true)) return;
    ensure_output();
    if (!mask()) throw std::runtime_error("Video cover unavailable");
    requested_ = capture_.start(source_, output_);
    next_path_check_ = GetTickCount64() + 1000U;
    notice(requested_ ? L"첫 게임 창 프레임을 기다립니다. 투컴 영상 미검증." : L"선택한 창을 캡처하지 못했습니다. 출력은 검은 화면입니다.");
    shown_ = WindowCaptureStatus::Starting;
}
void VideoOutputPanel::ensure_output()
{
    if (output_) {
        if (!output_intact()) throw std::runtime_error("Existing output changed");
        return;
    }
    const auto &r = output_bounds_;
    output_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kOutputClass, L"ChatView · 창 영상 출력",
        WS_POPUP | WS_CLIPCHILDREN, r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!output_) throw std::runtime_error("Video output unavailable");
    // Reuse the registered BLACK_BRUSH window class: SS_BLACKRECT uses the
    // theme-dependent window-frame color, not guaranteed RGB(0,0,0). Keep this
    // a separate owned HWND above, never GDI paint inside the flip surface.
    cover_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kOutputClass, L"",
        WS_POPUP | WS_VISIBLE, r.left, r.top, r.right - r.left, r.bottom - r.top,
        output_, nullptr, GetModuleHandleW(nullptr), this);
    if (!cover_) { DestroyWindow(output_); output_ = nullptr; throw std::runtime_error("Video cover unavailable"); }
    ShowWindow(output_, SW_SHOWNOACTIVATE);
}
bool VideoOutputPanel::output_intact() const noexcept
{
    RECT output{}, cover{};
    return output_ && cover_ && IsWindowVisible(output_) && !IsIconic(output_) &&
        GetWindow(cover_, GW_OWNER) == output_ && GetWindowRect(output_, &output) &&
        GetWindowRect(cover_, &cover) && equal(output, output_bounds_) && equal(cover, output_bounds_);
}
bool VideoOutputPanel::mask() noexcept
{
    if (!output_) return true;
    if (!cover_ || !IsWindow(cover_)) return false;
    if (!SetWindowPos(cover_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) return false;
    return RedrawWindow(cover_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW) != FALSE;
}
void VideoOutputPanel::invalidate_choices() noexcept
{
    ++selection_epoch_;
    sources_.clear(); monitors_.clear();
    if (panel_) {
        SendDlgItemMessageW(panel_, kSource, CB_RESETCONTENT, 0, 0);
        SendDlgItemMessageW(panel_, kMonitor, CB_RESETCONTENT, 0, 0);
    }
}
void VideoOutputPanel::interrupt(const wchar_t *message) noexcept
{
    releasing_ = false;
    stop(message);
    invalidate_choices();
}
void VideoOutputPanel::stop(const wchar_t *message) noexcept
{
    requested_ = false;
    const bool covered = mask();
    capture_.stop();
    notice(covered ? message : L"검은 덮개를 확인하지 못했습니다. 수신 PC의 장면을 즉시 중지하세요. 캡처 중지를 요청했습니다.");
}
void VideoOutputPanel::release() noexcept
{
    if (!output_ || releasing_ || confirming_ || closing_) return;
    stop(L"출력은 검은 화면으로 중지했습니다. 출력 창 해제를 확인하세요.");
    const HWND target = output_;
    const auto epoch = selection_epoch_;
    confirming_ = true;
    const int answer = MessageBoxW(panel_,
        L"검은 출력창을 닫으면 해당 화면의 바탕화면과 다른 창이 수신 영상에 보일 수 있습니다.\n\n"
        L"먼저 수신 PC에서 이 입력을 사용하는 방송/녹화 장면을 중지하세요.\n"
        L"챗뷰는 수신 PC의 중지 여부를 확인할 수 없습니다.\n\n출력 창을 닫을까요?",
        L"ChatView · 출력 창 해제 확인", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    confirming_ = false;
    releasing_ = answer == IDYES && !closing_ && target == output_ && epoch == selection_epoch_;
    if (!releasing_) notice(L"출력 창 해제를 취소했습니다. 검은 화면을 유지하며 영상은 자동 재시작하지 않습니다.");
}
void VideoOutputPanel::tick() noexcept
{
    if (!enabled_ || closing_) return;
    const auto now = GetTickCount64();
    const bool inspect_paths = now >= next_path_check_;
    if (requested_ && (!output_intact() || !topology(inspect_paths)))
        interrupt(L"출력/화면 구성·창/HUD 위치가 바뀌어 중지했습니다. 목록을 새로고침하고 직접 다시 선택하세요.");
    if (inspect_paths) next_path_check_ = now + 1000U;
    if (releasing_ && !capture_.running()) {
        if (output_) DestroyWindow(output_);
        output_ = nullptr; cover_ = nullptr; output_device_.clear(); releasing_ = false;
        notice(L"출력 창을 해제했습니다. 해당 화면의 바탕화면이 보일 수 있습니다.");
    }
    if (!requested_) return;
    const auto value = capture_.snapshot();
    if (value.status == WindowCaptureStatus::Capturing) ShowWindow(cover_, SW_HIDE);
    else if (!mask()) { stop(L"출력 보호를 확인하지 못했습니다."); return; }
    if (!capture_.running() && value.status != WindowCaptureStatus::Starting) {
        stop(L"창 종료·최소화 또는 캡처 오류로 중지했습니다. 출력은 검은 화면입니다. 직접 다시 선택하세요."); return;
    }
    if (value.status == shown_) return;
    shown_ = value.status;
    if (value.status == WindowCaptureStatus::Capturing) notice(L"선택 창 영상을 별도 출력 중 · 영상만/SDR · 실제 투컴 송출은 미검증");
    else if (value.status == WindowCaptureStatus::Waiting) notice(L"새 프레임 대기 · 오래된 영상은 검게 지웠습니다.");
    else if (!capture_.running()) stop(L"창 종료·최소화 또는 캡처 오류로 중지했습니다. 출력은 검은 화면입니다. 직접 다시 선택하세요.");
}
LRESULT CALLBACK VideoOutputPanel::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto *self = reinterpret_cast<VideoOutputPanel *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<VideoOutputPanel *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    try {
        if (message == WM_MOUSEACTIVATE && (window == self->output_ || window == self->cover_)) return MA_NOACTIVATE;
        if (message == WM_WTSSESSION_CHANGE && window == self->panel_) {
            if (wparam == WTS_SESSION_LOCK || wparam == WTS_CONSOLE_DISCONNECT ||
                wparam == WTS_REMOTE_DISCONNECT || wparam == WTS_SESSION_LOGOFF) self->session_blocked_ = true;
            else if (wparam == WTS_SESSION_UNLOCK || wparam == WTS_CONSOLE_CONNECT ||
                wparam == WTS_REMOTE_CONNECT || wparam == WTS_SESSION_LOGON) self->session_blocked_ = false;
            self->interrupt(L"로그인 세션이 바뀌어 검은 화면으로 중지했습니다. 복귀 뒤 목록을 새로고침하고 직접 시작하세요.");
            return 0;
        }
        if (message == WM_POWERBROADCAST && (wparam == PBT_APMSUSPEND ||
            wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND)) {
            self->suspended_ = wparam == PBT_APMSUSPEND;
            self->interrupt(L"전원 상태가 바뀌어 검은 화면으로 중지했습니다. 복귀 뒤 목록을 새로고침하고 직접 시작하세요.");
            return TRUE;
        }
        if (message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE ||
            (message == WM_DEVICECHANGE && (wparam == DBT_DEVNODES_CHANGED || wparam == DBT_DEVICEREMOVECOMPLETE))) {
            self->interrupt(L"화면/장치 구성이 바뀌어 검은 화면으로 중지했습니다. 목록을 새로고침하고 직접 다시 선택하세요.");
            return message == WM_DEVICECHANGE ? TRUE : 0;
        }
        if (message == WM_COMMAND) {
            if (window != self->panel_ || self->closing_ || self->confirming_) return 0;
            switch (LOWORD(wparam)) {
            case kRefresh: self->refresh(); return 0;
            case kStart: self->start(); return 0;
            case kStop: self->stop(L"출력 중지 · 검은 화면 유지 · 로컬 채팅은 계속 사용할 수 있습니다."); return 0;
            case kRelease: self->release(); return 0;
            case IDCANCEL: SendMessageW(window, WM_CLOSE, 0, 0); return 0;
            }
        }
        if (message == WM_CLOSE) {
            self->interrupt(L"창 닫기 요청으로 출력을 중지했습니다. 검은 출력창은 유지합니다.");
            if (window == self->panel_) ShowWindow(window, SW_HIDE);
            return 0;
        }
        if (message == WM_DESTROY && window == self->panel_ && self->notifications_) {
            WTSUnRegisterSessionNotification(window); self->notifications_ = false;
        }
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            if (self->panel_ == window) self->panel_ = nullptr;
            if (self->cover_ == window) self->cover_ = nullptr;
            if (self->output_ == window) { self->output_ = nullptr; self->cover_ = nullptr; }
        }
    } catch (...) { self->stop(L"출력 처리 실패 · 개인 채팅이나 전체 화면 캡처로 전환하지 않습니다."); }
    return DefWindowProcW(window, message, wparam, lparam);
}
void VideoOutputPanel::close() noexcept
{
    if (closing_) return;
    closing_ = true;
    stop(L"종료 중");
    // Keep HWNDs alive and controls fenced while close services synchronous
    // DXGI messages. Posted UI commands are not dispatched by this join.
    capture_.close();
    if (output_) DestroyWindow(output_);
    output_ = nullptr; cover_ = nullptr;
    if (panel_) DestroyWindow(panel_);
    panel_ = nullptr;
    if (hotkey_) UnregisterHotKey(nullptr, kVideoHotkey);
    hotkey_ = false;
}
}
