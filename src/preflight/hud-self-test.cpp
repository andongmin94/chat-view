// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/shared-state.hpp"
#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"

#include <Windows.h>
#include <WtsApi32.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

namespace {

constexpr wchar_t kHudWindowClass[] = L"ChatViewObsHudWindow";
constexpr wchar_t kCaptureProbeWindowClass[] =
    L"ChatViewObsCaptureProbeWindow";
constexpr DWORD kStartupTimeoutMs = 25000U;
constexpr DWORD kShutdownTimeoutMs = 8000U;
constexpr DWORD kWindowStateTimeoutMs = 3000U;
constexpr DWORD kCaptureSettleTimeMs = 180U;
constexpr int kCaptureProbeWidth = 240;
constexpr int kCaptureProbeHeight = 180;
constexpr int kMinimumProbeColorDistance = 120;
constexpr int kMaximumProtectedColorDistance = 36;
constexpr auto kProfileCleanupTimeout = std::chrono::seconds(10);
constexpr DWORD kJobGracefulDrainTimeoutMs = 2000U;
constexpr DWORD kJobForcedDrainTimeoutMs = 5000U;

#ifndef WDA_EXCLUDEFROMCAPTURE
constexpr DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
#endif

struct WindowSearch {
    DWORD process_id = 0U;
    HWND window = nullptr;
};

class MappedState final {
public:
    explicit MappedState(chatview::SharedState *state) noexcept : state_(state) {}

    ~MappedState()
    {
        if (state_ != nullptr) {
            UnmapViewOfFile(state_);
        }
    }

    MappedState(const MappedState &) = delete;
    MappedState &operator=(const MappedState &) = delete;

    [[nodiscard]] chatview::SharedState *get() const noexcept
    {
        return state_;
    }

private:
    chatview::SharedState *state_ = nullptr;
};

LRESULT CALLBACK capture_probe_window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC device = BeginPaint(window, &paint);
        if (device != nullptr) {
            RECT bounds{};
            GetClientRect(window, &bounds);
            const auto *color = reinterpret_cast<const COLORREF *>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
            if (color != nullptr) {
                SetDCBrushColor(device, *color);
                FillRect(
                    device,
                    &bounds,
                    reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            }
            EndPaint(window, &paint);
        }
        return 0L;
    }

    if (message == WM_ERASEBKGND) {
        return 1L;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

class CaptureProbe final {
public:
    CaptureProbe() = default;

    ~CaptureProbe()
    {
        if (foreground_ != nullptr) {
            DestroyWindow(foreground_);
        }
        if (background_ != nullptr) {
            DestroyWindow(background_);
        }
        if (class_registered_ && instance_ != nullptr) {
            UnregisterClassW(kCaptureProbeWindowClass, instance_);
        }
    }

    CaptureProbe(const CaptureProbe &) = delete;
    CaptureProbe &operator=(const CaptureProbe &) = delete;

    [[nodiscard]] bool create() noexcept
    {
        instance_ = GetModuleHandleW(nullptr);
        if (instance_ == nullptr) {
            return false;
        }

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &capture_probe_window_proc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kCaptureProbeWindowClass;
        if (RegisterClassExW(&window_class) == 0U) {
            return false;
        }
        class_registered_ = true;

        RECT work_area{};
        if (!SystemParametersInfoW(
                SPI_GETWORKAREA, 0U, &work_area, 0U)) {
            return false;
        }

        const int work_width =
            static_cast<int>(work_area.right - work_area.left);
        const int work_height =
            static_cast<int>(work_area.bottom - work_area.top);
        const int width = std::min(kCaptureProbeWidth, work_width);
        const int height = std::min(kCaptureProbeHeight, work_height);
        if (width < 32 || height < 32) {
            return false;
        }

        left_ = work_area.left +
                (work_area.right - work_area.left - width) / 2;
        top_ = work_area.top +
               (work_area.bottom - work_area.top - height) / 2;
        sample_point_ = POINT{left_ + width / 2, top_ + height / 2};

        constexpr DWORD extended_style =
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        background_ = CreateWindowExW(
            extended_style,
            kCaptureProbeWindowClass,
            L"",
            WS_POPUP,
            left_,
            top_,
            width,
            height,
            nullptr,
            nullptr,
            instance_,
            &background_color_);
        if (background_ == nullptr) {
            return false;
        }

        foreground_ = CreateWindowExW(
            extended_style,
            kCaptureProbeWindowClass,
            L"",
            WS_POPUP,
            left_,
            top_,
            width,
            height,
            nullptr,
            nullptr,
            instance_,
            &foreground_color_);
        return foreground_ != nullptr;
    }

    [[nodiscard]] bool show_background() const noexcept
    {
        ShowWindow(foreground_, SW_HIDE);
        if (!SetWindowPos(
                background_,
                HWND_TOPMOST,
                left_,
                top_,
                0,
                0,
                SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
            return false;
        }
        return settle(background_);
    }

    [[nodiscard]] bool show_foreground() const noexcept
    {
        if (!SetWindowPos(
                foreground_,
                HWND_TOPMOST,
                left_,
                top_,
                0,
                0,
                SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
            return false;
        }
        return settle(foreground_);
    }

    [[nodiscard]] bool protect_foreground() const noexcept
    {
        if (!SetWindowDisplayAffinity(
                foreground_, WDA_EXCLUDEFROMCAPTURE)) {
            return false;
        }

        DWORD affinity = WDA_NONE;
        if (!GetWindowDisplayAffinity(foreground_, &affinity) ||
            affinity != WDA_EXCLUDEFROMCAPTURE) {
            return false;
        }
        return settle(foreground_);
    }

    [[nodiscard]] POINT sample_point() const noexcept
    {
        return sample_point_;
    }

private:
    [[nodiscard]] static bool settle(HWND window) noexcept
    {
        if (!RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN)) {
            return false;
        }
        const HRESULT flush_result = DwmFlush();
        if (FAILED(flush_result)) {
            return false;
        }
        Sleep(kCaptureSettleTimeMs);
        return true;
    }

    HINSTANCE instance_ = nullptr;
    HWND background_ = nullptr;
    HWND foreground_ = nullptr;
    bool class_registered_ = false;
    int left_ = 0;
    int top_ = 0;
    POINT sample_point_{};
    COLORREF background_color_ = RGB(24, 191, 83);
    COLORREF foreground_color_ = RGB(226, 39, 139);
};

[[nodiscard]] bool capture_screen_pixel(
    POINT point, COLORREF &color) noexcept
{
    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return false;
    }

    HDC memory = CreateCompatibleDC(screen);
    if (memory == nullptr) {
        ReleaseDC(nullptr, screen);
        return false;
    }

    HBITMAP bitmap = CreateCompatibleBitmap(screen, 1, 1);
    if (bitmap == nullptr) {
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return false;
    }

    HGDIOBJ previous = SelectObject(memory, bitmap);
    const bool selected = previous != nullptr && previous != HGDI_ERROR;
    const bool copied = selected &&
                        BitBlt(
                            memory,
                            0,
                            0,
                            1,
                            1,
                            screen,
                            point.x,
                            point.y,
                            SRCCOPY | CAPTUREBLT) != FALSE;
    const COLORREF sampled = copied ? GetPixel(memory, 0, 0) : CLR_INVALID;

    if (selected) {
        SelectObject(memory, previous);
    }
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    if (sampled == CLR_INVALID) {
        return false;
    }
    color = sampled;
    return true;
}

[[nodiscard]] int color_distance(
    COLORREF left, COLORREF right) noexcept
{
    return std::abs(
               static_cast<int>(GetRValue(left)) -
               static_cast<int>(GetRValue(right))) +
           std::abs(
               static_cast<int>(GetGValue(left)) -
               static_cast<int>(GetGValue(right))) +
           std::abs(
               static_cast<int>(GetBValue(left)) -
               static_cast<int>(GetBValue(right)));
}

[[nodiscard]] bool verify_capture_exclusion() noexcept
{
    CaptureProbe probe;
    if (!probe.create() || !probe.show_background()) {
        std::wcerr << L"The local capture probe could not create its calibration windows\n";
        return false;
    }

    COLORREF background_sample = CLR_INVALID;
    if (!capture_screen_pixel(probe.sample_point(), background_sample)) {
        std::wcerr << L"The local capture probe could not sample its background\n";
        return false;
    }

    if (!probe.show_foreground()) {
        std::wcerr << L"The local capture probe could not show its foreground\n";
        return false;
    }

    COLORREF foreground_sample = CLR_INVALID;
    if (!capture_screen_pixel(probe.sample_point(), foreground_sample)) {
        std::wcerr << L"The local capture probe could not sample its foreground\n";
        return false;
    }

    if (color_distance(background_sample, foreground_sample) <
        kMinimumProbeColorDistance) {
        std::wcerr << L"Desktop capture did not distinguish the probe windows\n";
        return false;
    }

    if (!probe.protect_foreground()) {
        std::wcerr << L"Windows did not enable capture exclusion for the probe\n";
        return false;
    }

    COLORREF protected_sample = CLR_INVALID;
    if (!capture_screen_pixel(probe.sample_point(), protected_sample)) {
        std::wcerr << L"The local capture probe could not sample the protected window\n";
        return false;
    }

    const int background_distance =
        color_distance(protected_sample, background_sample);
    const int foreground_distance =
        color_distance(protected_sample, foreground_sample);
    if (background_distance > kMaximumProtectedColorDistance ||
        background_distance >= foreground_distance) {
        std::wcerr
            << L"Capture exclusion did not reveal the calibrated background "
               L"behind the protected window\n";
        return false;
    }
    return true;
}

BOOL CALLBACK find_hud_window(HWND window, LPARAM data)
{
    auto *search = reinterpret_cast<WindowSearch *>(data);
    if (search == nullptr) {
        return FALSE;
    }

    DWORD process_id = 0U;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != search->process_id) {
        return TRUE;
    }

    std::array<wchar_t, 64U> class_name{};
    const int length =
        GetClassNameW(window, class_name.data(), static_cast<int>(class_name.size()));
    if (length > 0 && wcscmp(class_name.data(), kHudWindowClass) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HWND wait_for_hud_window(HANDLE process, DWORD process_id) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kWindowStateTimeoutMs;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(process, 0U) != WAIT_TIMEOUT) {
            return nullptr;
        }

        WindowSearch search{process_id, nullptr};
        EnumWindows(&find_hud_window, reinterpret_cast<LPARAM>(&search));
        if (search.window != nullptr) {
            return search.window;
        }
        Sleep(25U);
    }
    return nullptr;
}

bool wait_for_style(HWND window, LONG_PTR required, LONG_PTR forbidden) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kWindowStateTimeoutMs;
    while (GetTickCount64() < deadline) {
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if ((style & required) == required && (style & forbidden) == 0) {
            return true;
        }
        Sleep(25U);
    }
    return false;
}

bool wait_for_visibility(
    HWND window, bool visible) noexcept
{
    const ULONGLONG deadline =
        GetTickCount64() + kWindowStateTimeoutMs;
    while (GetTickCount64() < deadline) {
        if ((IsWindowVisible(window) != FALSE) == visible) {
            return true;
        }
        Sleep(25U);
    }
    return false;
}

bool wait_for_width_greater(HWND window, LONG previous_width) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kWindowStateTimeoutMs;
    while (GetTickCount64() < deadline) {
        RECT bounds{};
        if (GetWindowRect(window, &bounds) &&
            bounds.right - bounds.left > previous_width) {
            return true;
        }
        Sleep(25U);
    }
    return false;
}

bool remove_tree_with_retry(const std::filesystem::path &path) noexcept
{
    const auto deadline =
        std::chrono::steady_clock::now() + kProfileCleanupTimeout;
    std::error_code error;

    do {
        error.clear();
        std::filesystem::remove_all(path, error);
        if (!error && !std::filesystem::exists(path, error)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (std::chrono::steady_clock::now() < deadline);

    return false;
}

bool configure_child_job(HANDLE job) noexcept
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return SetInformationJobObject(
               job,
               JobObjectExtendedLimitInformation,
               &limits,
               static_cast<DWORD>(sizeof(limits))) != FALSE;
}

bool wait_for_job_empty(HANDLE job, DWORD timeout_ms) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (!QueryInformationJobObject(
                job,
                JobObjectBasicAccountingInformation,
                &accounting,
                static_cast<DWORD>(sizeof(accounting)),
                nullptr)) {
            return false;
        }
        if (accounting.ActiveProcesses == 0U) {
            return true;
        }
        Sleep(50U);
    } while (GetTickCount64() < deadline);
    return false;
}

bool drain_child_job(HANDLE job) noexcept
{
    if (wait_for_job_empty(job, kJobGracefulDrainTimeoutMs)) {
        return true;
    }
    if (!TerminateJobObject(job, 0U)) {
        return false;
    }
    return wait_for_job_empty(job, kJobForcedDrainTimeoutMs);
}

void publish(
    chatview::SharedState *state,
    HANDLE event,
    std::uint32_t flags) noexcept
{
    InterlockedIncrement(&state->sequence);
    MemoryBarrier();
    state->flags = flags;
    ++state->generation;
    MemoryBarrier();
    InterlockedIncrement(&state->sequence);
    SetEvent(event);
}

int fail(const wchar_t *message, HANDLE process = nullptr)
{
    std::wcerr << message << L'\n';
    if (process != nullptr &&
        WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
        TerminateProcess(process, 1U);
        WaitForSingleObject(process, 2000U);
    }
    return 1;
}

int fail_process_exit(HANDLE process)
{
    DWORD exit_code = 0U;
    if (!GetExitCodeProcess(process, &exit_code)) {
        return fail(L"The HUD exited before reporting readiness");
    }

    std::wcerr << L"The HUD exited before reporting readiness (exit code "
               << exit_code << L")\n";
    return 1;
}

} // namespace

int wmain(int argument_count, wchar_t **arguments)
{
    if (argument_count != 2) {
        return fail(L"Expected the HUD executable path");
    }

    const std::filesystem::path hud_path = arguments[1];
    if (!std::filesystem::is_regular_file(hud_path)) {
        return fail(L"The HUD executable does not exist");
    }

    if (!verify_capture_exclusion()) {
        return fail(
            L"This Windows capture path did not pass the privacy preflight");
    }

    const DWORD process_id = GetCurrentProcessId();
    const std::wstring suffix = std::to_wstring(process_id);
    const std::wstring mapping_name =
        L"Local\\ChatViewOBS.Test.State." + suffix;
    const std::wstring event_name =
        L"Local\\ChatViewOBS.Test.Event." + suffix;
    const std::wstring ready_event_name =
        L"Local\\ChatViewOBS.Test.Ready." + suffix;

    chatview::UniqueHandle mapping(CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0U,
        static_cast<DWORD>(sizeof(chatview::SharedState)),
        mapping_name.c_str()));
    if (!mapping) {
        return fail(L"Failed to create the smoke-test mapping");
    }

    MappedState mapped_state(static_cast<chatview::SharedState *>(
        MapViewOfFile(
            mapping.get(),
            FILE_MAP_ALL_ACCESS,
            0U,
            0U,
            sizeof(chatview::SharedState))));
    if (mapped_state.get() == nullptr) {
        return fail(L"Failed to map the smoke-test state");
    }

    chatview::UniqueHandle state_event(
        CreateEventW(nullptr, FALSE, FALSE, event_name.c_str()));
    if (!state_event) {
        return fail(L"Failed to create the smoke-test event");
    }

    chatview::UniqueHandle ready_event(
        CreateEventW(nullptr, TRUE, FALSE, ready_event_name.c_str()));
    if (!ready_event) {
        return fail(L"Failed to create the smoke-test readiness event");
    }

    ZeroMemory(mapped_state.get(), sizeof(chatview::SharedState));
    mapped_state.get()->magic = chatview::kSharedStateMagic;
    mapped_state.get()->version = chatview::kSharedStateVersion;
    mapped_state.get()->flags =
        chatview::SharedStateStreaming |
        chatview::SharedStateCaptureRisk;
    mapped_state.get()->generation = 1U;

    const std::filesystem::path local_app_data =
        std::filesystem::temp_directory_path() /
        (L"chatview-hud-smoke-" + suffix);
    if (!remove_tree_with_retry(local_app_data)) {
        return fail(L"Failed to reset the smoke-test profile directory");
    }

    std::error_code error;
    std::filesystem::create_directories(local_app_data, error);
    if (error) {
        return fail(L"Failed to create the smoke-test profile directory");
    }

    const std::wstring local_app_data_string = local_app_data.wstring();
    if (!SetEnvironmentVariableW(
            L"LOCALAPPDATA", local_app_data_string.c_str())) {
        return fail(L"Failed to redirect the smoke-test profile");
    }

    std::wstring command_line =
        L"\"" + hud_path.wstring() + L"\" --mapping \"" + mapping_name +
        L"\" --event \"" + event_name + L"\" --ready-event \"" +
        ready_event_name + L"\" --parent " + suffix;

    chatview::UniqueHandle child_job(
        CreateJobObjectW(nullptr, nullptr));
    if (!child_job || !configure_child_job(child_job.get())) {
        return fail(L"Failed to create the HUD process job");
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION child_info{};
    if (!CreateProcessW(
            hud_path.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
            nullptr,
            nullptr,
            &startup_info,
            &child_info)) {
        return fail(L"Failed to start the HUD executable");
    }

    chatview::UniqueHandle child_thread(child_info.hThread);
    chatview::UniqueHandle child_process(child_info.hProcess);
    if (!AssignProcessToJobObject(
            child_job.get(), child_process.get())) {
        TerminateProcess(child_process.get(), 1U);
        WaitForSingleObject(child_process.get(), 2000U);
        return fail(L"Failed to assign the HUD to its process job");
    }
    if (ResumeThread(child_thread.get()) == static_cast<DWORD>(-1)) {
        TerminateJobObject(child_job.get(), 1U);
        WaitForSingleObject(child_process.get(), 2000U);
        return fail(L"Failed to resume the HUD process");
    }
    child_thread.reset();

    HANDLE startup_handles[2] = {ready_event.get(), child_process.get()};
    const DWORD startup_result =
        WaitForMultipleObjects(
            2U, startup_handles, FALSE, kStartupTimeoutMs);
    if (startup_result == WAIT_OBJECT_0 + 1U) {
        return fail_process_exit(child_process.get());
    }
    if (startup_result != WAIT_OBJECT_0) {
        return fail(
            L"The WebView2 HUD did not report readiness",
            child_process.get());
    }

    HWND window =
        wait_for_hud_window(child_process.get(), child_info.dwProcessId);
    if (window == nullptr) {
        return fail(
            L"The ready HUD window could not be enumerated",
            child_process.get());
    }
    if (IsWindowVisible(window)) {
        return fail(
            L"The HUD ignored capture suppression during startup",
            child_process.get());
    }

    constexpr LONG_PTR locked_style =
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
        WS_EX_NOREDIRECTIONBITMAP;
    if (!wait_for_style(window, locked_style, 0)) {
        return fail(
            L"The HUD did not enter locked overlay mode",
            child_process.get());
    }

    DWORD affinity = 0U;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"The HUD did not request capture exclusion",
            child_process.get());
    }

    publish(
        mapped_state.get(),
        state_event.get(),
        chatview::SharedStateStreaming);
    if (!wait_for_visibility(window, true)) {
        return fail(
            L"The HUD did not return after capture risk cleared",
            child_process.get());
    }

    const UINT toggle_edit_message =
        RegisterWindowMessageW(chatview::kToggleEditMessageName);
    if (toggle_edit_message == 0U) {
        return fail(
            L"Failed to register the overlay edit control message",
            child_process.get());
    }

    if (!PostMessageW(window, toggle_edit_message, 0U, 0L)) {
        return fail(
            L"Failed to request HUD edit mode",
            child_process.get());
    }
    if (!wait_for_style(
            window,
            WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) {
        return fail(
            L"The HUD did not enter interactive edit mode",
            child_process.get());
    }

    RECT edit_bounds{};
    if (!GetWindowRect(window, &edit_bounds)) {
        return fail(
            L"Failed to read the edit-mode HUD bounds",
            child_process.get());
    }
    const LONG edit_width = edit_bounds.right - edit_bounds.left;
    const LONG edit_height = edit_bounds.bottom - edit_bounds.top;
    SetWindowPos(
        window,
        nullptr,
        edit_bounds.left,
        edit_bounds.top,
        edit_width + 120,
        edit_height + 80,
        SWP_NOZORDER | SWP_NOACTIVATE);
    PostMessageW(window, WM_EXITSIZEMOVE, 0U, 0L);
    if (!wait_for_width_greater(window, edit_width)) {
        return fail(
            L"The HUD did not accept a resized bound",
            child_process.get());
    }

    if (!PostMessageW(window, toggle_edit_message, 0U, 0L)) {
        return fail(
            L"Failed to request HUD lock mode",
            child_process.get());
    }
    if (!wait_for_style(window, locked_style, 0)) {
        return fail(
            L"The HUD did not return to locked mode",
            child_process.get());
    }

    publish(
        mapped_state.get(),
        state_event.get(),
        chatview::SharedStateStreaming |
            chatview::SharedStateCaptureRisk);
    if (!wait_for_visibility(window, false)) {
        return fail(
            L"The HUD remained visible after capture risk appeared",
            child_process.get());
    }
    if (WaitForSingleObject(child_process.get(), 0U) != WAIT_TIMEOUT) {
        return fail(
            L"Capture suppression terminated the HUD instead of hiding it");
    }
    if (!PostMessageW(window, toggle_edit_message, 0U, 0L)) {
        return fail(
            L"Failed to test editing during capture suppression",
            child_process.get());
    }
    Sleep(150U);
    if (IsWindowVisible(window) ||
        !wait_for_style(window, locked_style, 0)) {
        return fail(
            L"Capture suppression allowed the HUD to enter edit mode",
            child_process.get());
    }

    publish(
        mapped_state.get(),
        state_event.get(),
        chatview::SharedStateStreaming);
    if (!wait_for_visibility(window, true)) {
        if (WaitForSingleObject(child_process.get(), 0U) == WAIT_OBJECT_0) {
            DWORD exit_code = STILL_ACTIVE;
            GetExitCodeProcess(child_process.get(), &exit_code);
            std::wcerr
                << L"The HUD exited during the capture-suppression transition "
                << L"(exit code " << exit_code << L")\n";
            return 1;
        }
        return fail(
            L"The HUD did not resume after runtime capture suppression",
            child_process.get());
    }

    if (SendMessageW(
            window, WM_POWERBROADCAST, PBT_APMSUSPEND, 0L) != TRUE ||
        !wait_for_visibility(window, false)) {
        return fail(
            L"The HUD did not hide for Windows suspend",
            child_process.get());
    }
    if (!PostMessageW(window, toggle_edit_message, 0U, 0L)) {
        return fail(
            L"Failed to test editing during Windows suspend",
            child_process.get());
    }
    Sleep(150U);
    if (IsWindowVisible(window) ||
        !wait_for_style(window, locked_style, 0)) {
        return fail(
            L"Windows suspend allowed the HUD to become interactive",
            child_process.get());
    }
    if (SendMessageW(
            window,
            WM_POWERBROADCAST,
            PBT_APMRESUMEAUTOMATIC,
            0L) != TRUE) {
        return fail(
            L"The HUD rejected the Windows resume notification",
            child_process.get());
    }
    Sleep(200U);
    if (IsWindowVisible(window)) {
        return fail(
            L"The HUD became visible before resume revalidation",
            child_process.get());
    }
    if (!wait_for_visibility(window, true)) {
        return fail(
            L"The HUD did not return after Windows resume",
            child_process.get());
    }
    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Windows resume did not restore capture exclusion",
            child_process.get());
    }

    if (SendMessageW(
            window,
            WM_POWERBROADCAST,
            PBT_APMRESUMECRITICAL,
            0L) != TRUE) {
        return fail(
            L"The HUD rejected a critical resume without prior suspend",
            child_process.get());
    }
    Sleep(200U);
    if (IsWindowVisible(window)) {
        return fail(
            L"The HUD skipped critical-resume revalidation",
            child_process.get());
    }
    if (!wait_for_visibility(window, true)) {
        return fail(
            L"The HUD did not return after critical-resume revalidation",
            child_process.get());
    }
    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Critical resume did not retain capture exclusion",
            child_process.get());
    }

    SendMessageW(
        window, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0L);
    if (!wait_for_visibility(window, false)) {
        return fail(
            L"The HUD did not hide for session lock",
            child_process.get());
    }
    SendMessageW(
        window, WM_POWERBROADCAST, PBT_APMSUSPEND, 0L);
    SendMessageW(
        window,
        WM_POWERBROADCAST,
        PBT_APMRESUMEAUTOMATIC,
        0L);
    Sleep(200U);
    if (IsWindowVisible(window)) {
        return fail(
            L"Power resume bypassed an active session lock",
            child_process.get());
    }
    SendMessageW(
        window, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0L);
    if (!wait_for_visibility(window, true)) {
        return fail(
            L"The HUD did not return after session unlock",
            child_process.get());
    }
    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Session unlock did not restore capture exclusion",
            child_process.get());
    }

    if (SendMessageW(
            window, WM_QUERYENDSESSION, 0U, 0L) != TRUE ||
        !wait_for_visibility(window, false)) {
        return fail(
            L"The HUD did not hide for the Windows end-session query",
            child_process.get());
    }
    SendMessageW(window, WM_ENDSESSION, FALSE, 0L);
    Sleep(200U);
    if (IsWindowVisible(window)) {
        return fail(
            L"The HUD became visible before cancelled-shutdown revalidation",
            child_process.get());
    }
    if (!wait_for_visibility(window, true)) {
        return fail(
            L"The HUD did not return after shutdown was cancelled",
            child_process.get());
    }
    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Cancelled shutdown did not restore capture exclusion",
            child_process.get());
    }

    const std::filesystem::path placement_file =
        local_app_data / L"ChatView" / L"hud.ini";
    if (!std::filesystem::is_regular_file(placement_file)) {
        return fail(
            L"Locking the HUD did not persist its bounds",
            child_process.get());
    }
    const UINT saved_width = GetPrivateProfileIntW(
        L"placement", L"width_dip", 0, placement_file.c_str());
    const UINT saved_height = GetPrivateProfileIntW(
        L"placement", L"height_dip", 0, placement_file.c_str());
    if (saved_width <= 420U || saved_height <= 640U) {
        return fail(
            L"The resized HUD dimensions were not persisted",
            child_process.get());
    }

    publish(
        mapped_state.get(),
        state_event.get(),
        chatview::SharedStateStreaming | chatview::SharedStateShutdown);
    if (WaitForSingleObject(
            child_process.get(), kShutdownTimeoutMs) != WAIT_OBJECT_0) {
        return fail(
            L"The HUD did not exit after the shutdown state",
            child_process.get());
    }

    DWORD exit_code = 0U;
    if (!GetExitCodeProcess(child_process.get(), &exit_code) ||
        exit_code != 0U) {
        return fail(L"The HUD exited with an error");
    }

    if (!drain_child_job(child_job.get())) {
        return fail(L"Failed to drain the HUD process tree");
    }
    child_process.reset();
    child_job.reset();

    if (!remove_tree_with_retry(local_app_data)) {
        return fail(L"Failed to remove the smoke-test profile directory");
    }
    return 0;
}
