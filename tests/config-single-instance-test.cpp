// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/win32-handle.hpp"

#include <Windows.h>

#include <array>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace {

constexpr wchar_t kSettingsWindowClass[] = L"ChatViewObsConfigWindow";
constexpr DWORD kWindowTimeoutMs = 5000U;
constexpr DWORD kProcessExitTimeoutMs = 5000U;

struct WindowSearch {
    DWORD process_id = 0U;
    HWND window = nullptr;
};

struct ChildProcess {
    chatview::UniqueHandle process;
    DWORD process_id = 0U;
};

int fail(const wchar_t *message, HANDLE process = nullptr)
{
    std::wcerr << message << L'\n';
    if (process != nullptr && WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
        TerminateProcess(process, 1U);
        WaitForSingleObject(process, 2000U);
    }
    return 1;
}

BOOL CALLBACK find_settings_window(HWND window, LPARAM data)
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
    if (length > 0 && wcscmp(class_name.data(), kSettingsWindowClass) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

ChildProcess start_process(const std::filesystem::path &executable)
{
    std::wstring command_line = L"\"" + executable.wstring() + L"\"";
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

HWND wait_for_window(const ChildProcess &process)
{
    const ULONGLONG deadline = GetTickCount64() + kWindowTimeoutMs;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(process.process.get(), 0U) != WAIT_TIMEOUT) {
            return nullptr;
        }

        WindowSearch search{process.process_id, nullptr};
        EnumWindows(&find_settings_window, reinterpret_cast<LPARAM>(&search));
        if (search.window != nullptr && IsWindowVisible(search.window)) {
            return search.window;
        }
        Sleep(25U);
    }
    return nullptr;
}

bool exited_successfully(HANDLE process, DWORD timeout_ms)
{
    if (WaitForSingleObject(process, timeout_ms) != WAIT_OBJECT_0) {
        return false;
    }

    DWORD exit_code = 1U;
    return GetExitCodeProcess(process, &exit_code) && exit_code == 0U;
}

} // namespace

int wmain(int argument_count, wchar_t **arguments)
{
    if (argument_count != 2) {
        return fail(L"Expected the settings executable path");
    }

    const std::filesystem::path executable =
        std::filesystem::absolute(arguments[1]);
    if (!std::filesystem::is_regular_file(executable)) {
        return fail(L"The settings executable does not exist");
    }

    const std::filesystem::path profile =
        std::filesystem::temp_directory_path() /
        (L"chatview-settings-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(profile, error);
    error.clear();
    std::filesystem::create_directories(profile, error);
    if (error) {
        return fail(L"Failed to create the settings test profile");
    }

    const std::wstring profile_string = profile.wstring();
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA", profile_string.c_str())) {
        return fail(L"Failed to redirect the settings test profile");
    }

    ChildProcess first = start_process(executable);
    if (!first.process) {
        return fail(L"Failed to start the first settings process");
    }

    const HWND first_window = wait_for_window(first);
    if (first_window == nullptr) {
        return fail(L"The first settings window did not appear", first.process.get());
    }

    ChildProcess second = start_process(executable);
    if (!second.process) {
        return fail(L"Failed to start the second settings process", first.process.get());
    }

    if (!exited_successfully(second.process.get(), kProcessExitTimeoutMs)) {
        TerminateProcess(second.process.get(), 1U);
        return fail(
            L"The second settings process did not delegate to the existing window",
            first.process.get());
    }

    if (WaitForSingleObject(first.process.get(), 0U) != WAIT_TIMEOUT ||
        !IsWindow(first_window)) {
        return fail(
            L"Starting a second settings process closed the first one",
            first.process.get());
    }

    if (!PostMessageW(first_window, WM_CLOSE, 0U, 0L)) {
        return fail(L"Failed to close the first settings window", first.process.get());
    }
    if (!exited_successfully(first.process.get(), kProcessExitTimeoutMs)) {
        return fail(L"The first settings process did not exit cleanly", first.process.get());
    }

    error.clear();
    std::filesystem::remove_all(profile, error);
    if (error) {
        return fail(L"Failed to remove the settings test profile");
    }
    return 0;
}
