// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/video-output-panel.hpp"
#include "hud/hud-window.hpp"
#include "hud/video-layout.hpp"
#include "hud/video-output-pattern.hpp"
#include "hud/video-frame-time.hpp"
#include <dwmapi.h>
#include <wtsapi32.h>
#include <dbt.h>
#include <bcrypt.h>
#include <cwchar>
#include <stdexcept>

namespace chatview {
namespace {
constexpr int kVideoHotkey = 0x4356;
constexpr int kSource = 201, kMonitor = 202, kRefresh = 203, kStart = 204, kStop = 205, kRelease = 206, kNotice = 207, kIdentify = 208;
constexpr UINT_PTR kPatternTimer = 0x435650;
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
            CW_USEDEFAULT, CW_USEDEFAULT, 660, 490, nullptr, nullptr, klass.hInstance, this);
        if (!panel_) return;
        if (!SetWindowDisplayAffinity(panel_, WDA_EXCLUDEFROMCAPTURE)) { DestroyWindow(panel_); panel_ = nullptr; return; }
        notifications_ = WTSRegisterSessionNotification(panel_, NOTIFY_FOR_THIS_SESSION) != FALSE;
        if (!notifications_) { DestroyWindow(panel_); panel_ = nullptr; return; }
        const auto dpi = GetDpiForWindow(panel_);
        const auto scale = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
        SetWindowPos(panel_, nullptr, 0, 0, scale(660), scale(490), SWP_NOMOVE | SWP_NOZORDER);
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
        add(L"BUTTON", L"1. 시험 패턴 표시", BS_PUSHBUTTON | WS_TABSTOP, kIdentify, 20, 177, 235, 32);
        add(L"BUTTON", L"2. 수신 확인 · 게임 출력", BS_PUSHBUTTON | WS_TABSTOP, kStart, 270, 177, 360, 32);
        add(L"BUTTON", L"목록 새로고침", BS_PUSHBUTTON | WS_TABSTOP, kRefresh, 20, 223, 190, 32);
        add(L"BUTTON", L"중지 · 검은 화면", BS_PUSHBUTTON | WS_TABSTOP, kStop, 225, 223, 195, 32);
        add(L"BUTTON", L"출력 창 닫기", BS_PUSHBUTTON | WS_TABSTOP, kRelease, 435, 223, 195, 32);
        add(L"STATIC", L"시험 패턴의 표시 번호·테두리·움직임을 수신 PC에서 직접 확인하세요.\n번호 입력/기기등록은 없습니다. 게임은 확인 후에만 캡처합니다.\n육안 확인은 HUD 제외나 수신 영상의 자동 검증이 아닙니다.\n영상만/SDR. 출력 창 해제·앱 종료 뒤에는 바탕화면이 보일 수 있습니다.", 0, 0, 20, 276, 610, 90);
        add(L"STATIC", L"대기 중 · 투컴 영상 미검증", 0, kNotice, 20, 377, 610, 72);
        EnableWindow(GetDlgItem(panel_, kStart), FALSE);
        refresh(); ShowWindow(panel_, SW_SHOWNORMAL); SetForegroundWindow(panel_);
    } catch (...) { if (panel_) DestroyWindow(panel_);
    panel_ = nullptr; }
}
void VideoOutputPanel::refresh()
{
    if (!permitted() || !notifications_) return;
    if (capture_.running() || requested_) { notice(L"출력을 중지한 뒤 목록을 갱신하세요."); return; }
    if (check_.active()) stop(L"시험 패턴을 중지했습니다. 다시 선택하세요.");
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
    // A failed insertion must not shift labels away from validated entries.
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
    notice(monitors_.empty() ? L"별도 확장 화면이 없습니다. 복제 화면을 출력 대상으로 사용하지 않습니다." : L"게임 창과 별도 출력 화면을 선택하세요. 자동 선택/재시작하지 않습니다.");
}
bool VideoOutputPanel::topology(bool inspect_paths) const noexcept
{
    try {
        if (!permitted() || !source_ || !monitor_ || MonitorFromWindow(source_, MONITOR_DEFAULTTONULL) != source_monitor_) return false;
        DWORD pid = 0, cloaked = 0;
        if (GetWindowThreadProcessId(source_, &pid) != source_thread_ || !pid || pid != source_process_ ||
            !IsWindowVisible(source_) || IsIconic(source_) ||
            FAILED(DwmGetWindowAttribute(source_, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked) return false;
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
void VideoOutputPanel::identify()
{
    if (capture_.running() || requested_ || releasing_ || confirming_ || !notifications_ || !permitted()) { notice(L"출력을 중지하고 창과 별도 화면을 선택하세요."); return; }
    if (check_.active()) stop(L"이전 시험 패턴을 중지했습니다.");
    const LRESULT source = SendDlgItemMessageW(panel_, kSource, CB_GETCURSEL, 0, 0);
    const LRESULT monitor = SendDlgItemMessageW(panel_, kMonitor, CB_GETCURSEL, 0, 0);
    if (source < 0 || monitor < 0 || static_cast<size_t>(source) >= sources_.size() || static_cast<size_t>(monitor) >= monitors_.size()) {
        notice(L"게임 창과 별도 확장 출력 화면을 직접 선택하세요."); return;
    }
    const auto chosen = sources_[static_cast<size_t>(source)];
    const auto destination = monitors_[static_cast<size_t>(monitor)];
    // Retargeting cannot release an existing black output implicitly.
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
    const int answer = MessageBoxW(panel_,
        L"선택한 확장 화면에 식별용 시험 패턴을 표시합니다. 아직 게임 창은 캡처하지 않습니다.\n\n"
        L"수신 PC의 시험/미리보기 장면에서 표시 번호, 전체 테두리와 움직임을 확인하세요.\n"
        L"패턴도 방송에 보일 수 있으므로 실제 송출 중에는 사용하지 마세요.\n\n시험 패턴을 표시할까요?",
        L"ChatView · 시험 패턴 표시 확인", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    confirming_ = false;
    if (answer != IDYES || epoch != selection_epoch_ || !topology(true)) return;
    ensure_output();
    show_pattern();
}
void VideoOutputPanel::show_pattern()
{
    if (capture_.running() || requested_ || !output_intact() || !mask()) throw std::runtime_error("Pattern output unavailable");
    std::uint32_t identifier = 0;
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&identifier), static_cast<ULONG>(sizeof(identifier)), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Pattern label unavailable");
    identifier &= 0xffffffU;
    if (!identifier) identifier = 1;
    const auto now = GetTickCount64();
    if (!check_.begin(now, selection_epoch_, identifier) || !SetTimer(cover_, kPatternTimer, 500, nullptr)) {
        mask(); throw std::runtime_error("Pattern timer unavailable");
    }
    next_path_check_ = now + 1000U;
    if (!RedrawWindow(cover_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW) ||
        !check_.ready(GetTickCount64(), selection_epoch_)) {
        mask(); throw std::runtime_error("Pattern paint unavailable");
    }
    const auto label = video_check_label(identifier);
    const std::wstring message = std::wstring(L"시험 패턴 ") + label.data() + L" · " + output_device_ +
        L"\n수신 화면에서 같은 번호·테두리·움직임을 본 뒤 '2. 수신 확인'을 누르세요.\n2분 뒤 검게 중지합니다. 직접 육안 확인이며 자동 영상 검증이 아닙니다.";
    notice(message.c_str());
}
void VideoOutputPanel::paint_cover(HWND window) noexcept
{
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(window, &paint);
    RECT rect{};
    const bool checking = check_.active();
    bool painted = false;
    if (dc && GetClientRect(window, &rect)) {
        if (checking && check_.current(GetTickCount64(), selection_epoch_))
            painted = paint_video_output_pattern(dc, rect, check_.identifier(), check_.seconds(GetTickCount64()));
        if (!painted) FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
    EndPaint(window, &paint);
    if (!checking) return;
    check_.painted(painted);
    EnableWindow(GetDlgItem(panel_, kStart), painted ? TRUE : FALSE);
    // mask() clears the check before synchronous repaint; no recursive failure.
    if (!painted) stop(L"시험 패턴이 만료되거나 그리기에 실패했습니다. 검은 화면으로 중지합니다.");
}
void VideoOutputPanel::start()
{
    if (capture_.running() || requested_ || releasing_ || confirming_ || !notifications_ || !permitted()) { notice(L"현재 게임 출력을 시작할 수 없습니다."); return; }
    if (!check_.ready(GetTickCount64(), selection_epoch_) || !output_intact() || !IsWindowVisible(cover_) || !topology(true)) {
        stop(L"유효한 시험 패턴부터 표시하고 수신 화면에서 직접 확인하세요."); return;
    }
    const auto epoch = selection_epoch_;
    const auto label = video_check_label(check_.identifier());
    const std::wstring prompt = std::wstring(L"수신 화면에서 시험 패턴 ") + label.data() +
        L"의 전체 테두리와 움직이는 표시를 확인했습니까?\n\n"
        L"'예'를 누르면 선택한 게임 창 캡처를 시작합니다. 로컬 HUD는 유지합니다.\n"
        L"이 확인은 배선·HUD 제외·실제 수신 영상의 자동 검증이 아닙니다.\n\n선택한 게임 영상으로 전환할까요?";
    confirming_ = true;
    const int answer = MessageBoxW(panel_, prompt.c_str(), L"ChatView · 수신 화면 직접 확인", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    confirming_ = false;
    if (answer != IDYES || epoch != selection_epoch_ || !output_intact() || !IsWindowVisible(cover_) ||
        !topology(true)) {
        stop(L"게임 전환을 취소했거나 확인 상태가 바뀌었습니다. 새 시험 패턴부터 다시 확인하세요."); return;
    }
    begin_capture();
}
void VideoOutputPanel::begin_capture()
{
    // The worker-start boundary owns the single-use check, including restart.
    // Callers must not consume it themselves or revive a stopped selection.
    if (!check_.confirm(GetTickCount64(), selection_epoch_)) {
        stop(L"유효한 새 시험 패턴을 확인한 뒤 게임 출력을 다시 시작하세요."); return;
    }
    if (!mask()) throw std::runtime_error("Video cover unavailable");
    requested_ = capture_.start(source_, output_);
    if (!requested_) {
        interrupt(L"선택한 창의 캡처를 시작하지 못했습니다. 검은 출력창을 유지합니다. 목록을 새로고침하고 새 대상·시험 패턴부터 다시 선택하세요.");
        return;
    }
    next_path_check_ = GetTickCount64() + 1000U;
    notice(L"육안 확인 후 첫 게임 창 프레임을 기다립니다. 투컴 영상 미검증.");
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
    check_.reset();
    if (cover_) KillTimer(cover_, kPatternTimer);
    if (panel_) EnableWindow(GetDlgItem(panel_, kStart), FALSE);
    if (!output_) return true;
    if (!cover_ || !IsWindow(cover_)) return false;
    if (!SetWindowPos(cover_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) return false;
    return RedrawWindow(cover_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW) != FALSE;
}
void VideoOutputPanel::invalidate_choices() noexcept
{
    ++selection_epoch_;
    // Retire the captured window identity, but preserve the black output and
    // its monitor binding. A replacement target never releases that output.
    source_ = nullptr; source_process_ = 0; source_thread_ = 0; source_monitor_ = nullptr;
    sources_.clear(); monitors_.clear();
    if (panel_) {
        SendDlgItemMessageW(panel_, kSource, CB_RESETCONTENT, 0, 0);
        SendDlgItemMessageW(panel_, kMonitor, CB_RESETCONTENT, 0, 0);
    }
}
void VideoOutputPanel::interrupt(const wchar_t *message) noexcept
{
    stop(message);
    invalidate_choices();
}
void VideoOutputPanel::stop(const wchar_t *message) noexcept
{
    // A later request to keep black supersedes an accepted, pending release.
    // Retire it before masking can synchronously dispatch owner messages.
    releasing_ = false;
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
    if (releasing_) notice(L"출력 창 해제 예정 · 캡처 정리 후 닫습니다.\n닫히기 전에 '중지 · 검은 화면'을 누르면 해제를 취소합니다.");
    else notice(L"출력 창 해제를 취소했습니다. 검은 화면을 유지하며 영상은 자동 재시작하지 않습니다.");
}
void VideoOutputPanel::tick() noexcept
{
    if (!enabled_ || closing_) return;
    const auto now = GetTickCount64();
    const bool inspect_paths = now >= next_path_check_;
    if (check_.active() && !check_.current(now, selection_epoch_))
        stop(L"시험 패턴 확인 시간이 지났습니다. 검은 화면으로 중지하며 새 패턴부터 다시 확인하세요.");
    if ((requested_ || check_.active()) && (!output_intact() || !topology(inspect_paths)))
        interrupt(L"출력/화면 구성·창/HUD 위치가 바뀌어 중지했습니다. 목록을 새로고침하고 직접 다시 선택하세요.");
    if (inspect_paths) next_path_check_ = now + 1000U;
    if (releasing_ && !capture_.running()) {
        if (output_) DestroyWindow(output_);
        output_ = nullptr; cover_ = nullptr; output_device_.clear(); releasing_ = false;
        notice(L"출력 창을 해제했습니다. 해당 화면의 바탕화면이 보일 수 있습니다.");
    }
    update_capture(capture_.snapshot());
}
void VideoOutputPanel::update_capture(const WindowCaptureSnapshot &value) noexcept
{
    if (!requested_ || closing_) return;
    // Terminal status takes precedence over frame freshness and worker teardown.
    // Never briefly uncover an old Capturing snapshot from a finished worker.
    if (value.status == WindowCaptureStatus::SourceLost || value.status == WindowCaptureStatus::Failed ||
        value.status == WindowCaptureStatus::Stopped || !capture_.running()) {
        interrupt(L"창이 종료·최소화되었거나 캡처가 끝났습니다. 검은 출력창을 유지합니다. 목록을 새로고침하고 새 대상·시험 패턴부터 다시 선택하세요.");
        return;
    }
    // Do not trust a sticky Capturing flag while Present/driver work is slow.
    // The owner can cover stale content without waiting for the GPU worker.
    auto status = value.status;
    if (status == WindowCaptureStatus::Capturing && !video_frame_fresh(GetTickCount64(), value.content_at_ms))
        status = WindowCaptureStatus::Waiting;
    if (status == WindowCaptureStatus::Capturing) ShowWindow(cover_, SW_HIDE);
    else if (!mask()) { interrupt(L"출력 보호를 확인하지 못했습니다. 목록에서 다시 선택하세요."); return; }
    if (status == shown_) return;
    shown_ = status;
    if (status == WindowCaptureStatus::Capturing) notice(L"선택 창 영상을 별도 출력 중 · 영상만/SDR · 실제 투컴 송출은 미검증");
    else if (status == WindowCaptureStatus::Waiting) notice(L"새 프레임 대기 · 오래된 영상은 검게 지웠습니다.");
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
        if (message == WM_PAINT && window == self->cover_) { self->paint_cover(window); return 0; }
        if (message == WM_TIMER && window == self->cover_ && wparam == kPatternTimer) {
            // KillTimer does not remove already queued timer messages. Never
            // let one revive a stopped, expired or already-confirmed pattern.
            if (self->check_.active()) {
                self->tick();
                if (self->check_.active()) RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            }
            return 0;
        }
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
            if ((LOWORD(wparam) == kSource || LOWORD(wparam) == kMonitor) && HIWORD(wparam) == CBN_SELCHANGE) {
                ++self->selection_epoch_;
                self->stop(L"선택을 변경했습니다. 새 시험 패턴으로 수신 대상을 다시 확인하세요."); return 0;
            }
            switch (LOWORD(wparam)) {
            case kRefresh: self->refresh(); return 0;
            case kIdentify: self->identify(); return 0;
            case kStart: self->start(); return 0;
            case kStop:
                self->stop(self->releasing_
                    ? L"출력 창 해제를 취소했습니다. 검은 화면을 유지합니다.\n새 시험 패턴을 확인한 뒤 같은 출력창으로 재개하세요. 로컬 채팅은 유지합니다."
                    : L"출력 중지 · 검은 화면 유지 · 새 시험 패턴을 확인한 뒤 같은 출력창으로 재개하세요.\n로컬 채팅은 계속 사용할 수 있습니다.");
                return 0;
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
    // Keep HWNDs alive and controls fenced while servicing sent DXGI messages.
    capture_.close();
    if (output_) DestroyWindow(output_);
    output_ = nullptr; cover_ = nullptr;
    if (panel_) DestroyWindow(panel_);
    panel_ = nullptr;
    if (hotkey_) UnregisterHotKey(nullptr, kVideoHotkey);
    hotkey_ = false;
}
}
