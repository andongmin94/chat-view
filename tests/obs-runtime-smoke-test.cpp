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
constexpr DWORD kHudRestartTimeoutMs = 45000U;
constexpr DWORD kHudExitTimeoutMs = 10000U;

#ifndef WDA_EXCLUDEFROMCAPTURE
constexpr DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
#endif

struct WindowSearch {
    DWORD process_id = 0U;
    HWND window = nullptr;
};

struct HudInstance {
    DWORD process_id = 0U;
    chatview::UniqueHandle process;
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

DWORD find_child_process(DWORD parent_process_id, DWORD excluded_process_id) noexcept
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
            entry.th32ProcessID != excluded_process_id &&
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

bool wait_for_hud(
    HANDLE obs_process,
    DWORD obs_process_id,
    DWORD excluded_process_id,
    DWORD timeout_ms,
    HudInstance &instance) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(obs_process, 0U) != WAIT_TIMEOUT) {
            return false;
        }

        if (!instance.process) {
            const DWORD process_id =
                find_child_process(obs_process_id, excluded_process_id);
            if (process_id != 0U) {
                chatview::UniqueHandle process(OpenProcess(
                    SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                    FALSE,
                    process_id));
                if (process) {
                    instance.process_id = process_id;
                    instance.process = std::move(process);
                }
            }
        }

        if (instance.process) {
            if (WaitForSingleObject(instance.process.get(), 0U) != WAIT_TIMEOUT) {
                instance = {};
                Sleep(50U);
                continue;
            }

            instance.window = find_window(instance.process_id);
            if (instance.window != nullptr && IsWindowVisible(instance.window)) {
                return true;
            }
        }

        Sleep(50U);
    }
    return false;
}

bool validate_hud(const HudInstance &instance, const wchar_t *context)
{
    const LONG_PTR extended_style =
        GetWindowLongPtrW(instance.window, GWL_EXSTYLE);
    constexpr LONG_PTR required_style =
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
        WS_EX_NOREDIRECTIONBITMAP;
    if ((extended_style & required_style) != required_style) {
        std::wcerr << context << L" HUD was not in locked overlay mode\n";
        return false;
    }

    DWORD affinity = 0U;
    if (!GetWindowDisplayAffinity(instance.window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        std::wcerr << context << L" HUD did not retain capture exclusion\n";
        return false;
    }
    return true;
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

    HudInstance initial_hud;
    if (!wait_for_hud(
            obs_process.get(),
            process_info.dwProcessId,
            0U,
            kHudStartupTimeoutMs,
            initial_hud)) {
        DWORD exit_code = STILL_ACTIVE;
        GetExitCodeProcess(obs_process.get(), &exit_code);
        return fail(
            L"Official OBS Studio did not load a visible ChatView HUD (OBS exit code " +
                std::to_wstring(exit_code) + L")",
            obs_process.get());
    }
    if (!validate_hud(initial_hud, L"Initial")) {
        return fail(L"The initial ChatView HUD failed validation", obs_process.get());
    }

    const DWORD initial_process_id = initial_hud.process_id;
    if (!TerminateProcess(initial_hud.process.get(), 77U)) {
        return fail(
            L"The OBS integration test could not terminate the initial HUD",
            obs_process.get());
    }
    if (WaitForSingleObject(initial_hud.process.get(), kHudExitTimeoutMs) != WAIT_OBJECT_0) {
        return fail(
            L"The initial HUD did not terminate during the restart test",
            obs_process.get());
    }
    initial_hud = {};

    HudInstance restarted_hud;
    if (!wait_for_hud(
            obs_process.get(),
            process_info.dwProcessId,
            initial_process_id,
            kHudRestartTimeoutMs,
            restarted_hud)) {
        return fail(
            L"The ChatView plugin did not replace a crashed HUD runtime",
            obs_process.get());
    }
    if (restarted_hud.process_id == initial_process_id ||
        !validate_hud(restarted_hud, L"Restarted")) {
        return fail(
            L"The replacement ChatView HUD failed validation",
            obs_process.get());
    }

    if (!TerminateProcess(obs_process.get(), 0U)) {
        return fail(
            L"The OBS integration test could not terminate its isolated OBS process",
            obs_process.get());
    }
    WaitForSingleObject(obs_process.get(), 5000U);

    if (WaitForSingleObject(restarted_hud.process.get(), kHudExitTimeoutMs) != WAIT_OBJECT_0) {
        return fail(L"The replacement HUD remained after its OBS parent terminated");
    }

    return 0;
}
