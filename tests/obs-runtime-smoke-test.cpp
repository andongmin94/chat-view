// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kHudExecutableName[] = L"chat-view-hud.exe";
constexpr wchar_t kWebViewExecutableName[] = L"msedgewebview2.exe";
constexpr DWORD kHudStartupTimeoutMs = 45000U;
constexpr DWORD kHudRestartTimeoutMs = 45000U;
constexpr DWORD kHudExitTimeoutMs = 10000U;
constexpr DWORD kHudDescendantStartupTimeoutMs = 15000U;
constexpr DWORD kHudDescendantExitTimeoutMs = 15000U;
constexpr DWORD kObsExitTimeoutMs = 30000U;
constexpr unsigned int kCrashRecoveryCycles = 3U;

#ifndef WDA_EXCLUDEFROMCAPTURE
constexpr DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
#endif

struct WindowSearch {
    DWORD process_id = 0U;
    HWND window = nullptr;
};

struct ProcessWindowCollection {
    DWORD process_id = 0U;
    std::vector<HWND> windows;
};

struct ProcessRecord {
    DWORD process_id = 0U;
    DWORD parent_process_id = 0U;
    std::wstring executable;
};

struct TrackedProcess {
    DWORD process_id = 0U;
    std::wstring executable;
    chatview::UniqueHandle process;
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
    if (length > 0 &&
        wcscmp(class_name.data(), chatview::kHudWindowClassName) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK collect_process_windows(HWND window, LPARAM data)
{
    auto *collection = reinterpret_cast<ProcessWindowCollection *>(data);
    if (collection == nullptr || !IsWindowVisible(window)) {
        return TRUE;
    }

    DWORD process_id = 0U;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == collection->process_id) {
        collection->windows.push_back(window);
    }
    return TRUE;
}

std::vector<ProcessRecord> snapshot_processes() noexcept
{
    chatview::UniqueHandle snapshot(
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U));
    if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE) {
        return {};
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        return {};
    }

    std::vector<ProcessRecord> records;
    do {
        records.push_back(ProcessRecord{
            entry.th32ProcessID,
            entry.th32ParentProcessID,
            entry.szExeFile,
        });
    } while (Process32NextW(snapshot.get(), &entry));
    return records;
}

std::vector<TrackedProcess> open_descendant_processes(DWORD root_process_id) noexcept
{
    const std::vector<ProcessRecord> records = snapshot_processes();
    if (records.empty()) {
        return {};
    }

    std::vector<DWORD> lineage{root_process_id};
    std::vector<bool> included(records.size(), false);
    std::vector<TrackedProcess> descendants;

    bool added = false;
    do {
        added = false;
        for (std::size_t index = 0U; index < records.size(); ++index) {
            if (included[index]) {
                continue;
            }

            const ProcessRecord &record = records[index];
            if (std::find(
                    lineage.begin(), lineage.end(), record.parent_process_id) ==
                lineage.end()) {
                continue;
            }

            included[index] = true;
            lineage.push_back(record.process_id);
            added = true;

            chatview::UniqueHandle process(OpenProcess(
                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                record.process_id));
            if (process) {
                descendants.push_back(TrackedProcess{
                    record.process_id,
                    record.executable,
                    std::move(process),
                });
            }
        }
    } while (added);

    return descendants;
}

bool contains_webview_process(const std::vector<TrackedProcess> &processes) noexcept
{
    return std::any_of(
        processes.begin(), processes.end(), [](const TrackedProcess &process) {
            return _wcsicmp(
                       process.executable.c_str(), kWebViewExecutableName) == 0;
        });
}

bool wait_for_webview_descendants(
    HANDLE obs_process,
    DWORD hud_process_id,
    std::vector<TrackedProcess> &descendants) noexcept
{
    const ULONGLONG deadline =
        GetTickCount64() + kHudDescendantStartupTimeoutMs;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(obs_process, 0U) != WAIT_TIMEOUT) {
            return false;
        }

        std::vector<TrackedProcess> candidate =
            open_descendant_processes(hud_process_id);
        if (contains_webview_process(candidate)) {
            descendants = std::move(candidate);
            return true;
        }
        Sleep(50U);
    }
    return false;
}

bool wait_for_processes_exit(
    const std::vector<TrackedProcess> &processes, DWORD timeout_ms) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (const TrackedProcess &process : processes) {
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining =
            now >= deadline ? 0U : static_cast<DWORD>(deadline - now);
        if (WaitForSingleObject(process.process.get(), remaining) != WAIT_OBJECT_0) {
            std::wcerr << L"Descendant process remained alive: "
                       << process.executable << L" (PID "
                       << process.process_id << L")\n";
            return false;
        }
    }
    return true;
}

DWORD find_child_process(DWORD parent_process_id, DWORD excluded_process_id) noexcept
{
    const std::vector<ProcessRecord> records = snapshot_processes();
    for (const ProcessRecord &record : records) {
        if (record.parent_process_id == parent_process_id &&
            record.process_id != excluded_process_id &&
            _wcsicmp(record.executable.c_str(), kHudExecutableName) == 0) {
            return record.process_id;
        }
    }
    return 0U;
}

unsigned int count_child_hud_processes(DWORD parent_process_id) noexcept
{
    const std::vector<ProcessRecord> records = snapshot_processes();
    return static_cast<unsigned int>(std::count_if(
        records.begin(), records.end(), [parent_process_id](const ProcessRecord &record) {
            return record.parent_process_id == parent_process_id &&
                   _wcsicmp(record.executable.c_str(), kHudExecutableName) == 0;
        }));
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
                    SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION |
                        PROCESS_TERMINATE,
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

bool terminate_hud(HudInstance &instance, unsigned int cycle) noexcept
{
    if (!instance.process ||
        !TerminateProcess(instance.process.get(), 77U + cycle)) {
        return false;
    }
    if (WaitForSingleObject(instance.process.get(), kHudExitTimeoutMs) !=
        WAIT_OBJECT_0) {
        return false;
    }
    instance = {};
    return true;
}

bool request_graceful_obs_shutdown(
    HANDLE obs_process, DWORD obs_process_id) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kObsExitTimeoutMs;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(obs_process, 0U) == WAIT_OBJECT_0) {
            return true;
        }

        ProcessWindowCollection collection{obs_process_id, {}};
        EnumWindows(
            &collect_process_windows,
            reinterpret_cast<LPARAM>(&collection));

        for (const HWND window : collection.windows) {
            PostMessageW(window, WM_CLOSE, 0U, 0L);
        }

        if (WaitForSingleObject(obs_process, 250U) == WAIT_OBJECT_0) {
            return true;
        }
    }
    return false;
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

    HudInstance hud;
    if (!wait_for_hud(
            obs_process.get(),
            process_info.dwProcessId,
            0U,
            kHudStartupTimeoutMs,
            hud)) {
        DWORD exit_code = STILL_ACTIVE;
        GetExitCodeProcess(obs_process.get(), &exit_code);
        return fail(
            L"Official OBS Studio did not load a visible ChatView HUD (OBS exit code " +
                std::to_wstring(exit_code) + L")",
            obs_process.get());
    }
    if (!validate_hud(hud, L"Initial") ||
        count_child_hud_processes(process_info.dwProcessId) != 1U) {
        return fail(
            L"The initial ChatView HUD failed validation or was duplicated",
            obs_process.get());
    }

    for (unsigned int cycle = 0U; cycle < kCrashRecoveryCycles; ++cycle) {
        std::vector<TrackedProcess> old_descendants;
        if (!wait_for_webview_descendants(
                obs_process.get(), hud.process_id, old_descendants)) {
            return fail(
                L"The HUD did not own a WebView2 descendant before recovery cycle " +
                    std::to_wstring(cycle + 1U),
                obs_process.get());
        }

        const DWORD previous_process_id = hud.process_id;
        if (!terminate_hud(hud, cycle)) {
            return fail(
                L"The OBS integration test could not terminate HUD recovery cycle " +
                    std::to_wstring(cycle + 1U),
                obs_process.get());
        }

        if (!wait_for_processes_exit(
                old_descendants, kHudDescendantExitTimeoutMs)) {
            return fail(
                L"The previous HUD generation leaked descendant processes in cycle " +
                    std::to_wstring(cycle + 1U),
                obs_process.get());
        }

        if (!wait_for_hud(
                obs_process.get(),
                process_info.dwProcessId,
                previous_process_id,
                kHudRestartTimeoutMs,
                hud)) {
            return fail(
                L"The ChatView plugin did not recover HUD crash cycle " +
                    std::to_wstring(cycle + 1U),
                obs_process.get());
        }

        const std::wstring label =
            L"Recovered cycle " + std::to_wstring(cycle + 1U);
        if (hud.process_id == previous_process_id ||
            !validate_hud(hud, label.c_str()) ||
            count_child_hud_processes(process_info.dwProcessId) != 1U) {
            return fail(
                L"The replacement HUD failed validation in cycle " +
                    std::to_wstring(cycle + 1U),
                obs_process.get());
        }
    }

    std::vector<TrackedProcess> final_descendants;
    if (!wait_for_webview_descendants(
            obs_process.get(), hud.process_id, final_descendants)) {
        return fail(
            L"The final HUD did not own a WebView2 descendant before OBS shutdown",
            obs_process.get());
    }

    if (!request_graceful_obs_shutdown(
            obs_process.get(), process_info.dwProcessId)) {
        return fail(
            L"OBS Studio did not complete a graceful shutdown",
            obs_process.get());
    }

    DWORD obs_exit_code = 1U;
    if (!GetExitCodeProcess(obs_process.get(), &obs_exit_code) ||
        obs_exit_code != 0U) {
        return fail(
            L"OBS Studio did not exit cleanly (exit code " +
            std::to_wstring(obs_exit_code) + L")");
    }

    if (WaitForSingleObject(hud.process.get(), kHudExitTimeoutMs) !=
        WAIT_OBJECT_0) {
        return fail(L"The HUD remained after graceful OBS shutdown");
    }

    DWORD hud_exit_code = STILL_ACTIVE;
    if (!GetExitCodeProcess(hud.process.get(), &hud_exit_code) ||
        hud_exit_code != 0U) {
        return fail(
            L"The HUD did not exit cleanly with OBS (exit code " +
            std::to_wstring(hud_exit_code) + L")");
    }

    if (!wait_for_processes_exit(
            final_descendants, kHudDescendantExitTimeoutMs)) {
        return fail(L"The final HUD leaked WebView2 descendant processes after OBS shutdown");
    }

    return 0;
}
