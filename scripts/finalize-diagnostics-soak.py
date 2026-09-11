from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


Path("src/diagnostics/diagnostics-exporter.hpp").write_text(
    '''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace chatview {

struct DiagnosticsExportResult {
    bool success = false;
    std::filesystem::path directory;
    std::wstring error;
};

[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle() noexcept;
[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root) noexcept;

} // namespace chatview
''',
    encoding="utf-8",
    newline="\n",
)

replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''#include <algorithm>
#include <array>
#include <cstdint>
''',
    '''#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
''',
)
replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''#include <system_error>
#include <vector>

namespace chatview {
''',
    '''#include <system_error>
#include <utility>
#include <vector>

#define CHATVIEW_WIDEN_INNER(value) L##value
#define CHATVIEW_WIDEN(value) CHATVIEW_WIDEN_INNER(value)

namespace chatview {
''',
)
replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''std::filesystem::path create_export_directory()
{
    const std::filesystem::path desktop = desktop_directory();
    if (desktop.empty()) {
        return {};
    }
''',
    '''std::filesystem::path create_export_directory(
    const std::filesystem::path &output_root)
{
    if (output_root.empty()) {
        return {};
    }
''',
)
replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''        const std::filesystem::path candidate = desktop / candidate_name;
''',
    '''        const std::filesystem::path candidate = output_root / candidate_name;
''',
)
replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''DiagnosticsExportResult export_diagnostics_bundle() noexcept
{
    DiagnosticsExportResult result;
    try {
        result.directory = create_export_directory();
''',
    '''DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root) noexcept
{
    DiagnosticsExportResult result;
    try {
        result.directory = create_export_directory(output_root);
''',
)
replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''    return result;
}

} // namespace chatview
''',
    '''    return result;
}

DiagnosticsExportResult export_diagnostics_bundle() noexcept
{
    const std::filesystem::path desktop = desktop_directory();
    if (desktop.empty()) {
        DiagnosticsExportResult result;
        result.error = L"The Desktop diagnostics location could not be resolved.";
        return result;
    }
    return export_diagnostics_bundle_to(desktop);
}

} // namespace chatview
''',
)

replace_once(
    "CMakeLists.txt",
    '''add_executable(chat-view-diagnostics WIN32
    src/diagnostics/main.cpp
    src/diagnostics/diagnostics-exporter.cpp
    src/diagnostics/diagnostics-exporter.hpp
)
target_include_directories(
    chat-view-diagnostics PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-diagnostics PRIVATE
    chat-view-common
    chat-view-diagnostic-redaction
    bcrypt
    shell32
    user32
    version
)
target_compile_definitions(
    chat-view-diagnostics PRIVATE CHATVIEW_VERSION="${PROJECT_VERSION}")
chatview_enable_win32(chat-view-diagnostics)
set_target_properties(
    chat-view-diagnostics PROPERTIES OUTPUT_NAME "chat-view-diagnostics")
chatview_enable_warnings(chat-view-diagnostics)
chatview_set_runtime_output(chat-view-diagnostics)
''',
    '''add_library(chat-view-diagnostics-core STATIC
    src/diagnostics/diagnostics-exporter.cpp
    src/diagnostics/diagnostics-exporter.hpp
)
target_include_directories(
    chat-view-diagnostics-core PUBLIC "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-diagnostics-core PUBLIC
    chat-view-common
    chat-view-diagnostic-redaction
    bcrypt
    ole32
    shell32
    version
)
target_compile_definitions(
    chat-view-diagnostics-core PRIVATE CHATVIEW_VERSION="${PROJECT_VERSION}")
chatview_enable_win32(chat-view-diagnostics-core)
chatview_enable_warnings(chat-view-diagnostics-core)

add_executable(chat-view-diagnostics WIN32
    src/diagnostics/main.cpp
)
target_link_libraries(
    chat-view-diagnostics PRIVATE chat-view-diagnostics-core shell32 user32)
chatview_enable_win32(chat-view-diagnostics)
set_target_properties(
    chat-view-diagnostics PROPERTIES OUTPUT_NAME "chat-view-diagnostics")
chatview_enable_warnings(chat-view-diagnostics)
chatview_set_runtime_output(chat-view-diagnostics)
''',
)
replace_once(
    "CMakeLists.txt",
    '''    add_test(
        NAME chat-view-diagnostic-redaction
        COMMAND chat-view-diagnostic-redaction-test
    )

    add_executable(chat-view-control-status-reader-test
''',
    '''    add_test(
        NAME chat-view-diagnostic-redaction
        COMMAND chat-view-diagnostic-redaction-test
    )

    add_executable(chat-view-diagnostics-exporter-test
        tests/diagnostics-exporter-test.cpp
    )
    target_link_libraries(
        chat-view-diagnostics-exporter-test PRIVATE
        chat-view-diagnostics-core)
    chatview_enable_win32(chat-view-diagnostics-exporter-test)
    chatview_enable_warnings(chat-view-diagnostics-exporter-test)
    add_test(
        NAME chat-view-diagnostics-exporter
        COMMAND chat-view-diagnostics-exporter-test
    )

    add_executable(chat-view-control-status-reader-test
''',
)

replace_once(
    ".github/workflows/windows-build.yml",
    '''            'dist/obs-plugins/64bit/chat-view-config.exe',
            'dist/chat-view-self-test.exe',
''',
    '''            'dist/obs-plugins/64bit/chat-view-config.exe',
            'dist/obs-plugins/64bit/chat-view-diagnostics.exe',
            'dist/chat-view-self-test.exe',
''',
)
replace_once(
    ".github/workflows/windows-build.yml",
    '''            'dist/obs-plugins/64bit/chat-view-config.exe',
            'dist/chat-view-self-test.exe',
''',
    '''            'dist/obs-plugins/64bit/chat-view-config.exe',
            'dist/obs-plugins/64bit/chat-view-diagnostics.exe',
            'dist/chat-view-self-test.exe',
''',
)
replace_once(
    ".github/workflows/windows-build.yml",
    '''            (Join-Path $obsRoot 'obs-plugins/64bit/chat-view-config.exe'),
            (Join-Path $obsRoot 'data/obs-plugins/chat-view-obs/locale/en-US.ini'),
''',
    '''            (Join-Path $obsRoot 'obs-plugins/64bit/chat-view-config.exe'),
            (Join-Path $obsRoot 'obs-plugins/64bit/chat-view-diagnostics.exe'),
            (Join-Path $obsRoot 'data/obs-plugins/chat-view-obs/locale/en-US.ini'),
''',
)

replace_once(
    "src/plugin/runtime-controller.hpp",
    '''    [[nodiscard]] bool restart_hud() noexcept;
    [[nodiscard]] bool open_settings() const noexcept;
    [[nodiscard]] bool toggle_edit_mode() noexcept;
''',
    '''    [[nodiscard]] bool restart_hud() noexcept;
    [[nodiscard]] bool toggle_edit_mode() noexcept;
''',
)
replace_once(
    "src/plugin/runtime-controller.cpp",
    '''constexpr wchar_t kRuntimeExecutableName[] = L"chat-view-hud.exe";
constexpr wchar_t kSettingsExecutableName[] = L"chat-view-config.exe";
''',
    '''constexpr wchar_t kRuntimeExecutableName[] = L"chat-view-hud.exe";
''',
)
replace_once(
    "src/plugin/runtime-controller.cpp",
    '''bool RuntimeController::open_settings() const noexcept
{
    try {
        const std::wstring path = find_sibling_path(kSettingsExecutableName);
        if (path.empty() || !is_regular_file(path)) {
            blog(
                LOG_ERROR,
                "[ChatView OBS] Settings application was not found: %ls",
                path.c_str());
            return false;
        }

        std::wstring command_line = L"\\\"" + path + L"\\\"";
        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info{};
        if (!CreateProcessW(
                path.c_str(),
                command_line.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_UNICODE_ENVIRONMENT,
                nullptr,
                nullptr,
                &startup_info,
                &process_info)) {
            log_windows_error("CreateProcessW(settings)", GetLastError());
            return false;
        }

        UniqueHandle process(process_info.hProcess);
        UniqueHandle thread(process_info.hThread);
        return true;
    } catch (const std::exception &error) {
        blog(LOG_ERROR, "[ChatView OBS] Failed to open settings: %s", error.what());
    } catch (...) {
        blog(LOG_ERROR, "[ChatView OBS] Failed to open settings");
    }
    return false;
}

''',
    '',
)

replace_once(
    "README.md",
    '''- browser connectivity and provider connection-loss recovery;
- OBS **Tools → ChatView Settings...** configuration;
''',
    '''- browser connectivity and provider connection-loss recovery;
- fail-closed Windows suspend, session-lock, and shutdown recovery;
- privacy-filtered local diagnostics export with no automatic upload;
- bounded HUD/WebView2 process-tree resource soak tests;
- OBS **Tools → ChatView Settings...** configuration;
''',
)
replace_once(
    "README.md",
    '''The platform page probe sends a compact health heartbeat every two seconds, even when the detected state has not changed. It also reports browser offline/online transitions and provider reconnecting or disconnected banners from bounded structural status regions rather than ordinary chat messages. The native HUD watchdog reloads a configured chat page after twelve seconds without a valid heartbeat. A separately bounded connection-recovery policy gives a reported disconnect ten seconds to recover, reloads the page once, and restarts the HUD if the disconnect survives another fifteen seconds. Browser-offline time and Display Capture suppression time do not count toward either recovery deadline.

`WDA_EXCLUDEFROMCAPTURE`''',
    '''The platform page probe sends a compact health heartbeat every two seconds, even when the detected state has not changed. It also reports browser offline/online transitions and provider reconnecting or disconnected banners from bounded structural status regions rather than ordinary chat messages. The native HUD watchdog reloads a configured chat page after twelve seconds without a valid heartbeat. A separately bounded connection-recovery policy gives a reported disconnect ten seconds to recover, reloads the page once, and restarts the HUD if the disconnect survives another fifteen seconds. Browser-offline time and Display Capture suppression time do not count toward either recovery deadline.

Windows suspend, session lock, and shutdown-pending states hide the HUD immediately and disarm network recovery timers. After the final pause reason clears, ChatView waits for the desktop to settle, reapplies and verifies capture exclusion, restores monitor-relative bounds, and reloads the configured chat before showing the HUD. A cancelled Windows shutdown follows the same revalidation path.

**Tools → Export ChatView Diagnostics...** creates a local Desktop folder containing a version and binary-integrity summary, privacy-filtered ChatView-only OBS log lines, and an explicit privacy notice. The exporter omits chat messages and the configured URL, redacts URLs, user-profile paths, and IPC object values, and never uploads anything automatically.

`WDA_EXCLUDEFROMCAPTURE`''',
)
replace_once(
    "README.md",
    '''│   ├── chat-view-hud.exe
│   └── chat-view-config.exe
''',
    '''│   ├── chat-view-hud.exe
│   ├── chat-view-config.exe
│   └── chat-view-diagnostics.exe
''',
)
replace_once(
    "README.md",
    '''src/config/   Native settings application
data/locale/  OBS locale resources
''',
    '''src/config/   Native settings application
src/diagnostics/ Privacy-filtered local diagnostics exporter
data/locale/  OBS locale resources
''',
)
