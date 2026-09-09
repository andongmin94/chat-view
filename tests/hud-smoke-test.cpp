// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/shared-state.hpp"
#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

namespace {

constexpr wchar_t kHudWindowClass[] = L"ChatViewObsHudWindow";
constexpr DWORD kStartupTimeoutMs = 25000U;
constexpr DWORD kShutdownTimeoutMs = 8000U;
constexpr DWORD kWindowStateTimeoutMs = 3000U;
constexpr auto kProfileCleanupTimeout = std::chrono::seconds(10);

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

bool wait_for_width_greater(HWND window, LONG previous_width) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kWindowStateTimeoutMs;
    while (GetTickCount64() < deadline) {
        RECT bounds{};
        if (GetWindowRect(window, &bounds) && bounds.right - bounds.left > previous_width) {
            return true;
        }
        Sleep(25U);
    }
    return false;
}

bool remove_tree_with_retry(const std::filesystem::path &path) noexcept
{
    const auto deadline = std::chrono::steady_clock::now() + kProfileCleanupTimeout;
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

void publish(chatview::SharedState *state, HANDLE event, std::uint32_t flags) noexcept
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
    if (process != nullptr && WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
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

    const DWORD process_id = GetCurrentProcessId();
    const std::wstring suffix = std::to_wstring(process_id);
    const std::wstring mapping_name = L"Local\\ChatViewOBS.Test.State." + suffix;
    const std::wstring event_name = L"Local\\ChatViewOBS.Test.Event." + suffix;
    const std::wstring ready_event_name = L"Local\\ChatViewOBS.Test.Ready." + suffix;

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
    mapped_state.get()->flags = chatview::SharedStateStreaming;
    mapped_state.get()->generation = 1U;

    const std::filesystem::path local_app_data =
        std::filesystem::temp_directory_path() / (L"chatview-hud-smoke-" + suffix);
    if (!remove_tree_with_retry(local_app_data)) {
        return fail(L"Failed to reset the smoke-test profile directory");
    }

    std::error_code error;
    std::filesystem::create_directories(local_app_data, error);
    if (error) {
        return fail(L"Failed to create the smoke-test profile directory");
    }

    const std::wstring local_app_data_string = local_app_data.wstring();
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data_string.c_str())) {
        return fail(L"Failed to redirect the smoke-test profile");
    }

    std::wstring command_line =
        L"\"" + hud_path.wstring() + L"\" --mapping \"" + mapping_name +
        L"\" --event \"" + event_name + L"\" --ready-event \"" +
        ready_event_name + L"\" --parent " + suffix;

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION child_info{};
    if (!CreateProcessW(
            hud_path.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            nullptr,
            &startup_info,
            &child_info)) {
        return fail(L"Failed to start the HUD executable");
    }

    chatview::UniqueHandle child_thread(child_info.hThread);
    chatview::UniqueHandle child_process(child_info.hProcess);
    child_thread.reset();

    HANDLE startup_handles[2] = {ready_event.get(), child_process.get()};
    const DWORD startup_result =
        WaitForMultipleObjects(2U, startup_handles, FALSE, kStartupTimeoutMs);
    if (startup_result == WAIT_OBJECT_0 + 1U) {
        return fail_process_exit(child_process.get());
    }
    if (startup_result != WAIT_OBJECT_0) {
        return fail(L"The WebView2 HUD did not report readiness", child_process.get());
    }

    HWND window = wait_for_hud_window(child_process.get(), child_info.dwProcessId);
    if (window == nullptr) {
        return fail(L"The ready HUD window could not be enumerated", child_process.get());
    }
    if (!IsWindowVisible(window)) {
        return fail(L"The ready HUD window was not visible", child_process.get());
    }

    constexpr LONG_PTR locked_style =
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP;
    if (!wait_for_style(window, locked_style, 0)) {
        return fail(L"The HUD did not enter locked overlay mode", child_process.get());
    }

    DWORD affinity = 0U;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(L"The HUD did not request capture exclusion", child_process.get());
    }

    const UINT toggle_edit_message =
        RegisterWindowMessageW(chatview::kToggleEditMessageName);
    if (toggle_edit_message == 0U) {
        return fail(L"Failed to register the overlay edit control message", child_process.get());
    }

    if (!PostMessageW(window, toggle_edit_message, 0U, 0L)) {
        return fail(L"Failed to request HUD edit mode", child_process.get());
    }
    if (!wait_for_style(
            window,
            WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) {
        return fail(L"The HUD did not enter interactive edit mode", child_process.get());
    }

    RECT edit_bounds{};
    if (!GetWindowRect(window, &edit_bounds)) {
        return fail(L"Failed to read the edit-mode HUD bounds", child_process.get());
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
        return fail(L"The HUD did not accept a resized bound", child_process.get());
    }

    if (!PostMessageW(window, toggle_edit_message, 0U, 0L)) {
        return fail(L"Failed to request HUD lock mode", child_process.get());
    }
    if (!wait_for_style(window, locked_style, 0)) {
        return fail(L"The HUD did not return to locked mode", child_process.get());
    }

    const std::filesystem::path placement_file =
        local_app_data / L"ChatView" / L"hud.ini";
    if (!std::filesystem::is_regular_file(placement_file)) {
        return fail(L"Locking the HUD did not persist its bounds", child_process.get());
    }
    const UINT saved_width = GetPrivateProfileIntW(
        L"placement", L"width_dip", 0, placement_file.c_str());
    const UINT saved_height = GetPrivateProfileIntW(
        L"placement", L"height_dip", 0, placement_file.c_str());
    if (saved_width <= 420U || saved_height <= 640U) {
        return fail(L"The resized HUD dimensions were not persisted", child_process.get());
    }

    publish(
        mapped_state.get(),
        state_event.get(),
        chatview::SharedStateStreaming | chatview::SharedStateShutdown);
    if (WaitForSingleObject(child_process.get(), kShutdownTimeoutMs) != WAIT_OBJECT_0) {
        return fail(L"The HUD did not exit after the shutdown state", child_process.get());
    }

    DWORD exit_code = 0U;
    if (!GetExitCodeProcess(child_process.get(), &exit_code) || exit_code != 0U) {
        return fail(L"The HUD exited with an error");
    }

    if (!remove_tree_with_retry(local_app_data)) {
        return fail(L"Failed to remove the smoke-test profile directory");
    }
    return 0;
}
