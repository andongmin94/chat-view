// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/control-status.hpp"
#include "common/hud-health.hpp"
#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <system_error>

namespace {

constexpr wchar_t kControlCenterWindowClass[] = L"ChatViewObsConfigWindow";
constexpr wchar_t kFakeHudWindowClass[] = L"ChatViewObsHudWindow";
constexpr int kUrlEditId = 1001;
constexpr int kSaveButtonId = 1002;
constexpr int kEditButtonId = 1003;
constexpr int kRestartButtonId = 1004;
constexpr DWORD kWindowTimeoutMs = 8000U;
constexpr DWORD kProcessExitTimeoutMs = 5000U;

std::atomic_bool edit_message_received{false};
UINT edit_message = 0U;
UINT health_message = 0U;

class MappedStatus final {
public:
    explicit MappedStatus(chatview::ControlStatus *status) noexcept
        : status_(status)
    {
    }

    ~MappedStatus()
    {
        if (status_ != nullptr) {
            UnmapViewOfFile(status_);
        }
    }

    MappedStatus(const MappedStatus &) = delete;
    MappedStatus &operator=(const MappedStatus &) = delete;

    [[nodiscard]] chatview::ControlStatus *get() const noexcept
    {
        return status_;
    }

private:
    chatview::ControlStatus *status_ = nullptr;
};

struct ChildProcess {
    chatview::UniqueHandle process;
    DWORD process_id = 0U;
};

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

void pump_messages() noexcept
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool wait_until(
    const std::function<bool()> &predicate,
    DWORD timeout_ms) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
        pump_messages();
        if (predicate()) {
            return true;
        }
        Sleep(10U);
    }
    pump_messages();
    return predicate();
}

LRESULT CALLBACK fake_hud_window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == edit_message && edit_message != 0U) {
        edit_message_received.store(true, std::memory_order_release);
        return 0L;
    }
    if (message == health_message && health_message != 0U) {
        return static_cast<LRESULT>(chatview::encode_hud_health(
            chatview::HudHealthSnapshot{
                chatview::HudPageState::Ready,
                chatview::HudProvider::YouTube,
                0U}));
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

ChildProcess start_control_center(
    const std::filesystem::path &executable,
    const std::wstring &mapping_name,
    const std::wstring &status_event_name,
    const std::wstring &restart_event_name,
    DWORD parent_process_id)
{
    std::wstring command_line =
        L"\"" + executable.wstring() +
        L"\" --status-mapping \"" + mapping_name +
        L"\" --status-event \"" + status_event_name +
        L"\" --restart-event \"" + restart_event_name +
        L"\" --parent " + std::to_wstring(parent_process_id);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(
            executable.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            executable.parent_path().c_str(),
            &startup_info,
            &process_info)) {
        return {};
    }

    chatview::UniqueHandle thread(process_info.hThread);
    return ChildProcess{
        chatview::UniqueHandle(process_info.hProcess),
        process_info.dwProcessId};
}

HWND find_control_center(DWORD process_id) noexcept
{
    const HWND window = FindWindowW(kControlCenterWindowClass, nullptr);
    if (window == nullptr) {
        return nullptr;
    }

    DWORD owner_process_id = 0U;
    GetWindowThreadProcessId(window, &owner_process_id);
    return owner_process_id == process_id && IsWindowVisible(window)
               ? window
               : nullptr;
}

bool exited_successfully(HANDLE process) noexcept
{
    if (WaitForSingleObject(process, 0U) != WAIT_OBJECT_0) {
        return false;
    }

    DWORD exit_code = 1U;
    return GetExitCodeProcess(process, &exit_code) && exit_code == 0U;
}

void publish(
    chatview::ControlStatus *status,
    std::uint32_t flags,
    DWORD hud_process_id,
    std::uint64_t generation) noexcept
{
    InterlockedIncrement(&status->sequence);
    MemoryBarrier();
    status->flags = flags;
    status->hud_process_id = hud_process_id;
    status->generation = generation;
    status->updated_tick_ms = GetTickCount64();
    MemoryBarrier();
    InterlockedIncrement(&status->sequence);
}

bool child_text_contains(HWND parent, const wchar_t *needle)
{
    struct SearchContext {
        const wchar_t *needle = nullptr;
        bool found = false;
    } context{needle, false};

    EnumChildWindows(
        parent,
        [](HWND child, LPARAM data) -> BOOL {
            auto *context = reinterpret_cast<SearchContext *>(data);
            const int length = GetWindowTextLengthW(child);
            if (length <= 0) {
                return TRUE;
            }

            std::wstring text(
                static_cast<std::size_t>(length) + 1U, L'\0');
            const int copied = GetWindowTextW(
                child, text.data(), length + 1);
            if (copied > 0) {
                text.resize(static_cast<std::size_t>(copied));
                if (text.find(context->needle) != std::wstring::npos) {
                    context->found = true;
                    return FALSE;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
    return context.found;
}

bool post_command(HWND window, int identifier, HWND control) noexcept
{
    return PostMessageW(
               window,
               WM_COMMAND,
               MAKEWPARAM(identifier, BN_CLICKED),
               reinterpret_cast<LPARAM>(control)) != FALSE;
}

bool saved_url_matches(
    const std::filesystem::path &config_file,
    const wchar_t *expected) noexcept
{
    WritePrivateProfileStringW(
        nullptr, nullptr, nullptr, config_file.c_str());

    std::array<wchar_t, 256U> value{};
    const DWORD length = GetPrivateProfileStringW(
        L"chat",
        L"url",
        L"",
        value.data(),
        static_cast<DWORD>(value.size()),
        config_file.c_str());
    return length > 0U &&
           std::wstring(value.data(), length) == expected;
}

} // namespace

int wmain(int argument_count, wchar_t **arguments)
{
    if (argument_count != 2) {
        return fail(L"Expected the Control Center executable path");
    }

    const std::filesystem::path executable =
        std::filesystem::absolute(arguments[1]);
    if (!std::filesystem::is_regular_file(executable)) {
        return fail(L"The Control Center executable does not exist");
    }

    const DWORD process_id = GetCurrentProcessId();
    const std::wstring suffix = std::to_wstring(process_id);
    const std::wstring mapping_name =
        L"Local\\ChatViewOBS.Test.ControlCenter.Status." + suffix;
    const std::wstring status_event_name =
        L"Local\\ChatViewOBS.Test.ControlCenter.Changed." + suffix;
    const std::wstring restart_event_name =
        L"Local\\ChatViewOBS.Test.ControlCenter.Restart." + suffix;

    const std::filesystem::path profile =
        std::filesystem::temp_directory_path() /
        (L"chatview-control-center-test-" + suffix);
    std::error_code error;
    std::filesystem::remove_all(profile, error);
    error.clear();
    std::filesystem::create_directories(profile, error);
    if (error) {
        return fail(L"Failed to create the Control Center test profile");
    }
    const std::wstring profile_string = profile.wstring();
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA", profile_string.c_str())) {
        return fail(L"Failed to redirect the Control Center profile");
    }

    chatview::UniqueHandle mapping(CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0U,
        static_cast<DWORD>(sizeof(chatview::ControlStatus)),
        mapping_name.c_str()));
    if (!mapping) {
        return fail(L"Failed to create the Control Center status mapping");
    }

    MappedStatus mapped(static_cast<chatview::ControlStatus *>(
        MapViewOfFile(
            mapping.get(),
            FILE_MAP_ALL_ACCESS,
            0U,
            0U,
            sizeof(chatview::ControlStatus))));
    if (mapped.get() == nullptr) {
        return fail(L"Failed to map the Control Center status fixture");
    }

    chatview::UniqueHandle status_event(CreateEventW(
        nullptr, FALSE, FALSE, status_event_name.c_str()));
    chatview::UniqueHandle restart_event(CreateEventW(
        nullptr, FALSE, FALSE, restart_event_name.c_str()));
    if (!status_event || !restart_event) {
        return fail(L"Failed to create Control Center command events");
    }

    ZeroMemory(mapped.get(), sizeof(chatview::ControlStatus));
    mapped.get()->magic = chatview::kControlStatusMagic;
    mapped.get()->version = chatview::kControlStatusVersion;

    edit_message = RegisterWindowMessageW(
        chatview::kToggleEditMessageName);
    health_message = RegisterWindowMessageW(
        chatview::kQueryHudHealthMessageName);
    if (edit_message == 0U || health_message == 0U) {
        return fail(L"Failed to register fake HUD messages");
    }

    WNDCLASSEXW fake_hud_class{};
    fake_hud_class.cbSize = sizeof(fake_hud_class);
    fake_hud_class.lpfnWndProc = &fake_hud_window_proc;
    fake_hud_class.hInstance = GetModuleHandleW(nullptr);
    fake_hud_class.lpszClassName = kFakeHudWindowClass;
    if (RegisterClassExW(&fake_hud_class) == 0U &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return fail(L"Failed to register the fake HUD window class");
    }

    const HWND fake_hud = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kFakeHudWindowClass,
        L"ChatView Control Center test HUD",
        WS_POPUP,
        10,
        10,
        120,
        80,
        nullptr,
        nullptr,
        fake_hud_class.hInstance,
        nullptr);
    if (fake_hud == nullptr) {
        return fail(L"Failed to create the fake HUD window");
    }
    ShowWindow(fake_hud, SW_SHOWNOACTIVATE);

    publish(
        mapped.get(),
        chatview::ControlStatusHudRunning |
            chatview::ControlStatusHudVisible,
        process_id,
        1U);
    SetEvent(status_event.get());

    ChildProcess first = start_control_center(
        executable,
        mapping_name,
        status_event_name,
        restart_event_name,
        process_id);
    if (!first.process) {
        DestroyWindow(fake_hud);
        return fail(L"Failed to start the Control Center");
    }

    HWND control_center = nullptr;
    if (!wait_until(
            [&]() {
                control_center = find_control_center(first.process_id);
                return control_center != nullptr;
            },
            kWindowTimeoutMs)) {
        DestroyWindow(fake_hud);
        return fail(
            L"The Control Center window did not appear",
            first.process.get());
    }

    if (!wait_until(
            [&]() {
                return child_text_contains(
                           control_center, L"Connected to OBS Studio") &&
                       child_text_contains(
                           control_center, L"Private HUD safety active") &&
                       child_text_contains(
                           control_center, L"YouTube chat ready");
            },
            kWindowTimeoutMs)) {
        DestroyWindow(fake_hud);
        return fail(
            L"The Control Center did not render live OBS and HUD health",
            first.process.get());
    }

    const HWND url_edit = GetDlgItem(control_center, kUrlEditId);
    const HWND save_button = GetDlgItem(control_center, kSaveButtonId);
    const HWND edit_button = GetDlgItem(control_center, kEditButtonId);
    const HWND restart_button = GetDlgItem(control_center, kRestartButtonId);
    if (url_edit == nullptr || save_button == nullptr ||
        edit_button == nullptr || restart_button == nullptr) {
        DestroyWindow(fake_hud);
        return fail(
            L"The Control Center action controls were not created",
            first.process.get());
    }

    constexpr wchar_t kInputUrl[] =
        L"https://www.youtube.com/watch?v=dQw4w9WgXcQ";
    if (SendMessageW(
            url_edit,
            WM_SETTEXT,
            0U,
            reinterpret_cast<LPARAM>(kInputUrl)) == FALSE) {
        DestroyWindow(fake_hud);
        return fail(
            L"The Control Center URL field rejected WM_SETTEXT",
            first.process.get());
    }
    if (!post_command(control_center, kSaveButtonId, save_button)) {
        DestroyWindow(fake_hud);
        return fail(
            L"Save & Apply command could not be queued",
            first.process.get());
    }

    const std::filesystem::path config_file =
        profile / L"ChatView" / L"config.ini";
    if (!wait_until(
            [&]() {
                return child_text_contains(
                           control_center,
                           L"Saved and applied to the running HUD") &&
                       saved_url_matches(
                           config_file,
                           L"https://www.youtube.com/live_chat?is_popout=1&v=dQw4w9WgXcQ");
            },
            kWindowTimeoutMs)) {
        DestroyWindow(fake_hud);
        return fail(
            L"Save & Apply did not persist and acknowledge the canonical URL",
            first.process.get());
    }

    if (!post_command(control_center, kEditButtonId, edit_button) ||
        !wait_until(
            []() {
                return edit_message_received.load(
                    std::memory_order_acquire);
            },
            kWindowTimeoutMs)) {
        DestroyWindow(fake_hud);
        return fail(
            L"Move / Resize did not reach the HUD window",
            first.process.get());
    }

    if (!post_command(
            control_center, kRestartButtonId, restart_button) ||
        !wait_until(
            [&]() {
                return WaitForSingleObject(
                           restart_event.get(), 0U) == WAIT_OBJECT_0;
            },
            kWindowTimeoutMs)) {
        DestroyWindow(fake_hud);
        return fail(
            L"Restart HUD did not reach the OBS command event",
            first.process.get());
    }

    ChildProcess second = start_control_center(
        executable,
        mapping_name,
        status_event_name,
        restart_event_name,
        process_id);
    if (!second.process ||
        !wait_until(
            [&]() {
                return exited_successfully(second.process.get());
            },
            kProcessExitTimeoutMs)) {
        DestroyWindow(fake_hud);
        if (second.process) {
            TerminateProcess(second.process.get(), 1U);
        }
        return fail(
            L"A second Control Center instance did not delegate",
            first.process.get());
    }

    if (!IsWindow(control_center) ||
        WaitForSingleObject(first.process.get(), 0U) != WAIT_TIMEOUT) {
        DestroyWindow(fake_hud);
        return fail(
            L"Opening a second instance closed the active Control Center",
            first.process.get());
    }

    if (!PostMessageW(control_center, WM_CLOSE, 0U, 0L) ||
        !wait_until(
            [&]() {
                return exited_successfully(first.process.get());
            },
            kProcessExitTimeoutMs)) {
        DestroyWindow(fake_hud);
        return fail(
            L"The Control Center did not exit cleanly",
            first.process.get());
    }

    DestroyWindow(fake_hud);
    UnregisterClassW(kFakeHudWindowClass, fake_hud_class.hInstance);

    error.clear();
    std::filesystem::remove_all(profile, error);
    if (error) {
        return fail(L"Failed to remove the Control Center test profile");
    }
    return 0;
}
