from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


QUALIFICATION_TEST = r'''// SPDX-License-Identifier: GPL-2.0-or-later

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
'''


def main() -> None:
    write_text(
        ROOT / "tests/obs-capture-qualification-test.cpp",
        QUALIFICATION_TEST,
    )

    plugin = ROOT / "tests/obs-capture-qualification-plugin.cpp"
    replace_once(
        plugin,
        "HWND window, UINT message, WPARAM, LPARAM lparam)",
        "HWND window, UINT message, WPARAM wparam, LPARAM lparam)",
    )
    replace_once(
        plugin,
        "return DefWindowProcW(window, message, 0U, lparam);",
        "return DefWindowProcW(window, message, wparam, lparam);",
    )

    cmake = ROOT / "CMakeLists.txt"
    replace_once(
        cmake,
        "project(chat-view-obs VERSION 0.2.10 LANGUAGES CXX)",
        "project(chat-view-obs VERSION 0.2.11 LANGUAGES CXX)",
    )

    cmake_anchor = '''    add_executable(chat-view-obs-runtime-test
        tests/obs-runtime-smoke-test.cpp
        src/common/win32-handle.hpp
    )
    target_include_directories(chat-view-obs-runtime-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_win32(chat-view-obs-runtime-test)
    chatview_enable_warnings(chat-view-obs-runtime-test)
endif()
'''
    cmake_replacement = '''    add_executable(chat-view-obs-runtime-test
        tests/obs-runtime-smoke-test.cpp
        src/common/win32-handle.hpp
    )
    target_include_directories(chat-view-obs-runtime-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_win32(chat-view-obs-runtime-test)
    chatview_enable_warnings(chat-view-obs-runtime-test)

    add_library(chat-view-obs-capture-qualification MODULE
        tests/obs-capture-qualification-plugin.cpp
    )
    target_link_libraries(
        chat-view-obs-capture-qualification PRIVATE
        OBS::libobs
        OBS::obs-frontend-api
        dwmapi
        user32
    )
    chatview_enable_win32(chat-view-obs-capture-qualification)
    set_target_properties(
        chat-view-obs-capture-qualification PROPERTIES
        PREFIX ""
        OUTPUT_NAME "chat-view-obs-capture-qualification"
    )
    chatview_enable_warnings(chat-view-obs-capture-qualification)
    chatview_set_runtime_output(chat-view-obs-capture-qualification)

    add_executable(chat-view-obs-capture-qualification-test
        tests/obs-capture-qualification-test.cpp
        src/common/win32-handle.hpp
    )
    target_include_directories(
        chat-view-obs-capture-qualification-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}"
    )
    target_link_libraries(
        chat-view-obs-capture-qualification-test PRIVATE user32)
    chatview_enable_win32(chat-view-obs-capture-qualification-test)
    chatview_enable_warnings(chat-view-obs-capture-qualification-test)
endif()
'''
    replace_once(cmake, cmake_anchor, cmake_replacement)

    workflow = ROOT / ".github/workflows/windows-build.yml"
    replace_once(
        workflow,
        "          $runtimeTest = (Resolve-Path 'build/windows-x64/RelWithDebInfo/chat-view-obs-runtime-test.exe').Path\n",
        "          $runtimeTest = (Resolve-Path 'build/windows-x64/RelWithDebInfo/chat-view-obs-runtime-test.exe').Path\n"
        "          $qualificationTest = (Resolve-Path 'build/windows-x64/RelWithDebInfo/chat-view-obs-capture-qualification-test.exe').Path\n"
        "          $qualificationPlugin = (Resolve-Path 'build/windows-x64/rundir/obs-plugins/64bit/chat-view-obs-capture-qualification.dll').Path\n"
        "          $qualificationDestination = Join-Path $obsRoot 'obs-plugins/64bit/chat-view-obs-capture-qualification.dll'\n",
    )
    replace_once(
        workflow,
        "              & $runtimeTest $obsRoot\n",
        "              if ($cycle -eq 1) {\n"
        "                Copy-Item -LiteralPath $qualificationPlugin -Destination $qualificationDestination -Force\n"
        "                & $qualificationTest $obsRoot\n"
        "              }\n\n"
        "              & $runtimeTest $obsRoot\n",
    )
    replace_once(
        workflow,
        "              Start-Sleep -Milliseconds 500\n              & ./dist/uninstall.ps1 -ObsPath $obsRoot\n",
        "              Start-Sleep -Milliseconds 500\n"
        "              Remove-Item -LiteralPath $qualificationDestination -Force -ErrorAction SilentlyContinue\n"
        "              & ./dist/uninstall.ps1 -ObsPath $obsRoot\n",
    )

    readme = ROOT / "README.md"
    replace_once(
        readme,
        "- pinned Windows CI, native tests, installer test, and packaged artifact.\n",
        "- actual OBS Display Capture → scene → main-texture pixel qualification in official OBS Studio;\n"
        "- pinned Windows CI, native tests, installer test, and packaged artifact.\n",
    )
    replace_once(
        readme,
        "`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint. It does not remove the HUD from a physical HDMI signal sent to a capture card. As a second software-side barrier, ChatView hides the private HUD whenever OBS reports an active or showing Display Capture source while streaming, recording, replay buffering, or virtual-camera output is running. At output start it temporarily treats any configured Display Capture source as risky until OBS source activation settles. Failed starts expire automatically, and the HUD returns only after the risk condition clears. While hidden by that policy, the HUD pauses the periodic hidden-window affinity query. It explicitly reapplies and verifies capture exclusion before showing again, then verifies once more after restoration.\n",
        "`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint. It does not remove the HUD from a physical HDMI signal sent to a capture card. As a second software-side barrier, ChatView hides the private HUD whenever OBS reports an active or showing Display Capture source while streaming, recording, replay buffering, or virtual-camera output is running. At output start it temporarily treats any configured Display Capture source as risky until OBS source activation settles. Failed starts expire automatically, and the HUD returns only after the risk condition clears. While hidden by that policy, the HUD pauses the periodic hidden-window affinity query. It explicitly reapplies and verifies capture exclusion before showing again, then verifies once more after restoration.\n\nThe Windows workflow also loads a CI-only qualification module into the official OBS portable build. That module creates a real `monitor_capture` source, routes it through the current scene, reads back `obs_render_main_texture()`, and performs a calibrated foreground/background pixel comparison. The required assertion is that hiding the protected top-level window removes its pixels from the OBS program compositor. Whether Windows capture affinity is honored by the runner's selected DXGI/WGC path is recorded separately and is not confused with the fail-closed hide guarantee. This test covers Display Capture → scene → OBS main texture; it does not claim to validate encoder, muxer, streaming-service, or physical capture-card paths.\n",
    )

    architecture = ROOT / "docs/architecture.md"
    replace_once(
        architecture,
        "- capture-risk policy tests plus startup and runtime HUD hide/resume checks through the versioned shared-state transport;\n",
        "- capture-risk policy tests plus startup and runtime HUD hide/resume checks through the versioned shared-state transport;\n"
        "- a CI-only module loaded by official OBS Studio that creates a real Windows `monitor_capture` source, composes it through the current scene, reads back `obs_render_main_texture()`, calibrates visible foreground/background pixels, and requires a hidden protected window to disappear from the program compositor;\n",
    )
    replace_once(
        architecture,
        "These checks prevent publishing a package with a broken controller-to-HUD path. They do not replace interactive qualification on a real broadcaster workstation, GPU driver stack, game, and capture configuration.\n",
        "These checks prevent publishing a package with a broken controller-to-HUD path. The compositor qualification reaches the real Display Capture source and OBS main texture, but it stops before encoder, muxer, streaming-service, and physical HDMI paths. It therefore does not replace interactive qualification on a real broadcaster workstation, GPU driver stack, game, and capture configuration.\n",
    )


if __name__ == "__main__":
    main()
