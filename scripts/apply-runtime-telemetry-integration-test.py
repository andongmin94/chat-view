from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "tests/obs-runtime-smoke-test.cpp",
    '''#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"
''',
    '''#include "common/runtime-history-store.hpp"
#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"
''',
)
replace_once(
    "tests/obs-runtime-smoke-test.cpp",
    '''constexpr DWORD kObsExitTimeoutMs = 30000U;
constexpr unsigned int kCrashRecoveryCycles = 3U;
''',
    '''constexpr DWORD kObsExitTimeoutMs = 30000U;
constexpr DWORD kRuntimeHistoryTimeoutMs = 10000U;
constexpr unsigned int kCrashRecoveryCycles = 3U;
''',
)
replace_once(
    "tests/obs-runtime-smoke-test.cpp",
    '''bool terminate_hud(
    HudInstance &instance,
    unsigned int cycle) noexcept
{
''',
    '''bool wait_for_runtime_history(
    std::uint32_t expected_exit_code,
    std::uint32_t expected_failure_count) noexcept
{
    chatview::RuntimeHistoryStore store;
    const ULONGLONG deadline =
        GetTickCount64() + kRuntimeHistoryTimeoutMs;
    while (GetTickCount64() < deadline) {
        chatview::RuntimeTelemetrySnapshot telemetry;
        if (store.load(telemetry) &&
            chatview::has_runtime_telemetry_flag(
                telemetry,
                chatview::RuntimeTelemetryHistoryValid) &&
            chatview::has_runtime_telemetry_flag(
                telemetry,
                chatview::RuntimeTelemetryAutomatic) &&
            !chatview::has_runtime_telemetry_flag(
                telemetry,
                chatview::RuntimeTelemetryCircuitOpen) &&
            telemetry.last_exit_code == expected_exit_code &&
            telemetry.restart_reason ==
                chatview::RuntimeRestartReason::UnexpectedExit &&
            telemetry.consecutive_failures == expected_failure_count &&
            telemetry.event_filetime_utc != 0U) {
            return true;
        }
        Sleep(50U);
    }
    return false;
}

bool clear_runtime_history() noexcept
{
    chatview::RuntimeHistoryStore store;
    if (store.file_path().empty()) {
        return false;
    }
    if (DeleteFileW(store.file_path().c_str())) {
        return true;
    }
    return GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool terminate_hud(
    HudInstance &instance,
    unsigned int cycle) noexcept
{
''',
)
replace_once(
    "tests/obs-runtime-smoke-test.cpp",
    '''        if (!wait_for_processes_exit(
                old_descendants,
                kHudDescendantExitTimeoutMs)) {
            return fail(
                L"The previous HUD generation leaked WebView2 processes in cycle " +
                    std::to_wstring(cycle + 1U),
                obs_process.get());
        }

        if (!wait_for_hud(
''',
    '''        if (!wait_for_processes_exit(
                old_descendants,
                kHudDescendantExitTimeoutMs)) {
            return fail(
                L"The previous HUD generation leaked WebView2 processes in cycle " +
                    std::to_wstring(cycle + 1U),
                obs_process.get());
        }

        const std::uint32_t expected_exit_code = 77U + cycle;
        if (!wait_for_runtime_history(
                expected_exit_code,
                cycle + 1U)) {
            return fail(
                L"The ChatView plugin did not persist HUD recovery telemetry in cycle " +
                    std::to_wstring(cycle + 1U),
                obs_process.get());
        }

        if (!wait_for_hud(
''',
)
replace_once(
    "tests/obs-runtime-smoke-test.cpp",
    '''    std::vector<TrackedProcess> final_descendants;
''',
    '''    if (!clear_runtime_history()) {
        return fail(
            L"The OBS integration test could not reset its runtime-history fixture",
            obs_process.get());
    }

    std::vector<TrackedProcess> final_descendants;
''',
)

replace_once(
    "CMakeLists.txt",
    '''    add_executable(chat-view-obs-runtime-test
        tests/obs-runtime-smoke-test.cpp
        src/common/win32-handle.hpp
    )
    target_include_directories(chat-view-obs-runtime-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_win32(chat-view-obs-runtime-test)
''',
    '''    add_executable(chat-view-obs-runtime-test
        tests/obs-runtime-smoke-test.cpp
        src/common/win32-handle.hpp
    )
    target_include_directories(chat-view-obs-runtime-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(
        chat-view-obs-runtime-test PRIVATE chat-view-runtime-history)
    chatview_enable_win32(chat-view-obs-runtime-test)
''',
)
