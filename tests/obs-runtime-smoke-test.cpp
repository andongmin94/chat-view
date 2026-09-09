// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/win32-handle.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr wchar_t kHudExecutableName[] = L"chat-view-hud.exe";
constexpr wchar_t kHudWindowClass[] = L"ChatViewObsHudWindow";
constexpr DWORD kHudStartupTimeoutMs = 45000U;
constexpr DWORD kHudExitTimeoutMs = 10000U;

#ifndef WDA_EXCLUDEFROMCAPTURE
constexpr DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
#endif

struct WindowSearch {
    DWORD process_id = 0U;
    HWND window = nullptr;
};

int fail(const std::wstring &message, HANDLE obs_process = nullptr)
{
    std::wcerr << message << L'\n';
    if (obs_process != nullptr &&
        WaitForSingleObject(obs_process, 0U) == WAIT_TIMEOUT) {
        TerminateProcess(obs_process, 1U);
        WaitForSingleObject(obs_process, 3000U);
    }
    return 1;
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

DWORD find_child_process(DWORD parent_process_id) noexcept
{
    chatview::UniqueHandle snapshot(
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U));
    if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE) {
        return 0U;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        return 0U;
    }

    do {
        if (entry.th32ParentProcessID == parent_process_id &&
            _wcsicmp(entry.szExeFile, kHudExecutableName) == 0) {
            return entry.th32ProcessID;
        }
    } while (Process32NextW(snapshot.get(), &entry));

    return 0U;
}

HWND find_window(DWORD process_id) noexcept
{
    WindowSearch search{process_id, nullptr};
    EnumWindows(&find_hud_window, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

} // namespace

int wmain(int argument_count, wchar_t **arguments)
{
    if (argument_count != 2) {
        return fail(L"Expected the extracted OBS Studio root directory");
    }

    const std::filesystem::path obs_root =
        std::filesystem::absolute(arguments[1]);
    const std::filesystem::path obs_executable =
        obs_root / L"bin" / L"64bit" / L"obs64.exe";
    if (!std::filesystem::is_regular_file(obs_executable)) {
        return fail(L"The official OBS Studio executable was not found");
    }

    std::wstring command_line =
        L"\"" + obs_executable.wstring() +
        L"\" --portable --multi --disable-updater --disable-missing-files-check";
    std::wstring working_directory = obs_executable.parent_path().wstring();

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(
            obs_executable.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            working_directory.c_str(),
            &startup_info,
            &process_info)) {
        return fail(
            L"Failed to start the official OBS Studio portable executable (error " +
            std::to_wstring(GetLastError()) + L")");
    }

    chatview::UniqueHandle obs_thread(process_info.hThread);
    chatview::UniqueHandle obs_process(process_info.hProcess);
    obs_thread.reset();

    DWORD hud_process_id = 0U;
    chatview::UniqueHandle hud_process;
    HWND hud_window = nullptr;
    const ULONGLONG deadline = GetTickCount64() + kHudStartupTimeoutMs;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(obs_process.get(), 0U) != WAIT_TIMEOUT) {
            DWORD exit_code = 0U;
            GetExitCodeProcess(obs_process.get(), &exit_code);
            return fail(
                L"OBS Studio exited before the ChatView HUD started (exit code " +
                std::to_wstring(exit_code) + L")");
        }

        if (hud_process_id == 0U) {
            hud_process_id = find_child_process(process_info.dwProcessId);
            if (hud_process_id != 0U) {
                hud_process.reset(OpenProcess(
                    SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE,
                    hud_process_id));
                if (!hud_process) {
                    return fail(
                        L"The ChatView HUD process could not be opened",
                        obs_process.get());
                }
            }
        }

        if (hud_process) {
            if (WaitForSingleObject(hud_process.get(), 0U) != WAIT_TIMEOUT) {
                DWORD exit_code = 0U;
                GetExitCodeProcess(hud_process.get(), &exit_code);
                return fail(
                    L"The ChatView HUD exited during real OBS startup (exit code " +
                        std::to_wstring(exit_code) + L")",
                    obs_process.get());
            }

            hud_window = find_window(hud_process_id);
            if (hud_window != nullptr && IsWindowVisible(hud_window)) {
                break;
            }
        }

        Sleep(50U);
    }

    if (!hud_process || hud_window == nullptr || !IsWindowVisible(hud_window)) {
        return fail(
            L"Official OBS Studio did not load a visible ChatView HUD within the timeout",
            obs_process.get());
    }

    const LONG_PTR extended_style =
        GetWindowLongPtrW(hud_window, GWL_EXSTYLE);
    constexpr LONG_PTR required_style =
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
        WS_EX_NOREDIRECTIONBITMAP;
    if ((extended_style & required_style) != required_style) {
        return fail(
            L"The HUD loaded by official OBS Studio was not in locked overlay mode",
            obs_process.get());
    }

    DWORD affinity = 0U;
    if (!GetWindowDisplayAffinity(hud_window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"The HUD loaded by official OBS Studio did not retain capture exclusion",
            obs_process.get());
    }

    if (!TerminateProcess(obs_process.get(), 0U)) {
        return fail(
            L"The OBS integration test could not terminate its isolated OBS process",
            obs_process.get());
    }
    WaitForSingleObject(obs_process.get(), 5000U);

    if (WaitForSingleObject(hud_process.get(), kHudExitTimeoutMs) != WAIT_OBJECT_0) {
        return fail(L"The ChatView HUD did not exit after its real OBS parent terminated");
    }

    DWORD hud_exit_code = 0U;
    if (!GetExitCodeProcess(hud_process.get(), &hud_exit_code) ||
        hud_exit_code != 0U) {
        return fail(
            L"The ChatView HUD returned an error after its real OBS parent terminated");
    }

    return 0;
}
