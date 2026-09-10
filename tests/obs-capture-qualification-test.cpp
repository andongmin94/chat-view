// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/win32-handle.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t kResultEnvironmentVariable[] =
    L"CHATVIEW_OBS_CAPTURE_QUALIFICATION_RESULT";
constexpr DWORD kQualificationTimeoutMs = 60000U;
constexpr DWORD kObsExitTimeoutMs = 30000U;

struct ProcessWindowCollection {
    DWORD process_id = 0U;
    std::vector<HWND> windows;
};

int fail(const std::wstring &message, HANDLE process = nullptr)
{
    std::wcerr << message << L'\n';
    if (process != nullptr &&
        WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
        TerminateProcess(process, 1U);
        WaitForSingleObject(process, 3000U);
    }
    return 1;
}

BOOL CALLBACK collect_process_windows(HWND window, LPARAM data)
{
    auto *collection =
        reinterpret_cast<ProcessWindowCollection *>(data);
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

bool request_graceful_obs_shutdown(
    HANDLE process, DWORD process_id) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kObsExitTimeoutMs;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(process, 0U) == WAIT_OBJECT_0) {
            return true;
        }

        ProcessWindowCollection collection{process_id, {}};
        EnumWindows(
            &collect_process_windows,
            reinterpret_cast<LPARAM>(&collection));
        for (const HWND window : collection.windows) {
            PostMessageW(window, WM_CLOSE, 0U, 0L);
        }

        if (WaitForSingleObject(process, 250U) == WAIT_OBJECT_0) {
            return true;
        }
    }
    return false;
}

bool read_text_file(
    const std::filesystem::path &path, std::string &content)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    content.assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    return stream.good() || stream.eof();
}

bool has_report_line(
    const std::string &report, const std::string &line)
{
    std::size_t offset = 0U;
    while (offset <= report.size()) {
        const std::size_t end = report.find('\n', offset);
        std::string_view candidate(
            report.data() + offset,
            (end == std::string::npos ? report.size() : end) - offset);
        if (!candidate.empty() && candidate.back() == '\r') {
            candidate.remove_suffix(1U);
        }
        if (candidate == line) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1U;
    }
    return false;
}

} // namespace

int wmain(int argument_count, wchar_t **arguments)
{
    if (argument_count != 2) {
        return fail(
            L"Expected the extracted OBS Studio root directory");
    }

    const std::filesystem::path obs_root =
        std::filesystem::absolute(arguments[1]);
    const std::filesystem::path obs_executable =
        obs_root / L"bin" / L"64bit" / L"obs64.exe";
    if (!std::filesystem::is_regular_file(obs_executable)) {
        return fail(L"The official OBS Studio executable was not found");
    }

    const std::filesystem::path result =
        std::filesystem::temp_directory_path() /
        (L"chatview-obs-capture-qualification-" +
         std::to_wstring(GetCurrentProcessId()) + L".txt");
    DeleteFileW(result.c_str());

    const std::wstring result_string = result.wstring();
    if (!SetEnvironmentVariableW(
            kResultEnvironmentVariable, result_string.c_str())) {
        return fail(L"Failed to configure the qualification result path");
    }

    std::wstring command_line =
        L"\"" + obs_executable.wstring() +
        L"\" --portable --multi --disable-updater "
        L"--disable-missing-files-check";
    std::wstring working_directory =
        obs_executable.parent_path().wstring();

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    const BOOL started = CreateProcessW(
        obs_executable.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        working_directory.c_str(),
        &startup_info,
        &process_info);
    SetEnvironmentVariableW(kResultEnvironmentVariable, nullptr);
    if (!started) {
        return fail(
            L"Failed to start official OBS Studio for capture qualification");
    }

    chatview::UniqueHandle obs_thread(process_info.hThread);
    chatview::UniqueHandle obs_process(process_info.hProcess);
    obs_thread.reset();

    std::string report;
    const ULONGLONG deadline =
        GetTickCount64() + kQualificationTimeoutMs;
    while (GetTickCount64() < deadline) {
        if (read_text_file(result, report)) {
            break;
        }
        if (WaitForSingleObject(obs_process.get(), 0U) == WAIT_OBJECT_0) {
            DWORD exit_code = 0U;
            GetExitCodeProcess(obs_process.get(), &exit_code);
            DeleteFileW(result.c_str());
            return fail(
                L"OBS Studio exited before capture qualification completed "
                L"(exit code " + std::to_wstring(exit_code) + L")");
        }
        Sleep(100U);
    }

    if (report.empty()) {
        DeleteFileW(result.c_str());
        return fail(
            L"OBS compositor capture qualification timed out",
            obs_process.get());
    }

    const bool passed =
        has_report_line(report, "PASS") &&
        has_report_line(
            report,
            "pipeline=monitor_capture>scene>main_texture") &&
        has_report_line(report, "calibration_visible=1") &&
        has_report_line(report, "hidden_window_excluded=1");
    if (!passed) {
        std::cerr << report;
        DeleteFileW(result.c_str());
        return fail(
            L"OBS compositor capture qualification reported failure",
            obs_process.get());
    }

    std::cout << report;
    if (!request_graceful_obs_shutdown(
            obs_process.get(), process_info.dwProcessId)) {
        DeleteFileW(result.c_str());
        return fail(
            L"OBS Studio did not shut down after capture qualification",
            obs_process.get());
    }

    DWORD exit_code = 1U;
    if (!GetExitCodeProcess(obs_process.get(), &exit_code) ||
        exit_code != 0U) {
        DeleteFileW(result.c_str());
        return fail(
            L"OBS Studio exited abnormally after capture qualification "
            L"(exit code " + std::to_wstring(exit_code) + L")");
    }

    DeleteFileW(result.c_str());
    return 0;
}
