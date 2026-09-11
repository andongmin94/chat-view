from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def replace_between(path_text: str, start: str, end: str, replacement: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    start_index = text.find(start)
    if start_index < 0:
        raise SystemExit(f"{path_text}: start marker not found: {start}")
    end_index = text.find(end, start_index)
    if end_index < 0:
        raise SystemExit(f"{path_text}: end marker not found: {end}")
    updated = text[:start_index] + replacement + text[end_index:]
    path.write_text(updated, encoding="utf-8", newline="\n")


Path("src/common/runtime-telemetry.hpp").write_text(
    r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kRuntimeExitCodeUnavailable = 0xffffffffU;
inline constexpr std::uint32_t kMaximumRuntimeFailureCount = 6U;

enum class RuntimeRestartReason : std::uint32_t {
    None = 0U,
    LaunchFailure = 1U,
    StartupTimeout = 2U,
    WebViewFailure = 3U,
    NavigationFailure = 4U,
    CaptureExclusionFailure = 5U,
    ReadySignalFailure = 6U,
    PlacementFailure = 7U,
    PageHealthTimeout = 8U,
    ConnectionRecovery = 9U,
    SystemLifecycleRecovery = 10U,
    UnexpectedExit = 11U,
    ManualRestart = 12U,
};

enum RuntimeTelemetryFlag : std::uint32_t {
    RuntimeTelemetryNone = 0U,
    RuntimeTelemetryHistoryValid = 1U << 0U,
    RuntimeTelemetryAutomatic = 1U << 1U,
    RuntimeTelemetryCircuitOpen = 1U << 2U,
};

inline constexpr std::uint32_t kKnownRuntimeTelemetryFlags =
    RuntimeTelemetryHistoryValid | RuntimeTelemetryAutomatic |
    RuntimeTelemetryCircuitOpen;

struct RuntimeTelemetrySnapshot {
    std::uint32_t last_exit_code = kRuntimeExitCodeUnavailable;
    RuntimeRestartReason restart_reason = RuntimeRestartReason::None;
    std::uint32_t consecutive_failures = 0U;
    std::uint32_t flags = RuntimeTelemetryNone;
    std::uint64_t event_filetime_utc = 0U;

    bool operator==(const RuntimeTelemetrySnapshot &) const = default;
};

[[nodiscard]] constexpr bool has_runtime_telemetry_flag(
    const RuntimeTelemetrySnapshot &snapshot,
    RuntimeTelemetryFlag flag) noexcept
{
    return (snapshot.flags & static_cast<std::uint32_t>(flag)) != 0U;
}

[[nodiscard]] constexpr bool is_valid_runtime_restart_reason(
    RuntimeRestartReason reason) noexcept
{
    return static_cast<std::uint32_t>(reason) <=
           static_cast<std::uint32_t>(RuntimeRestartReason::ManualRestart);
}

[[nodiscard]] constexpr bool is_valid_runtime_telemetry(
    const RuntimeTelemetrySnapshot &snapshot) noexcept
{
    if ((snapshot.flags & ~kKnownRuntimeTelemetryFlags) != 0U ||
        !is_valid_runtime_restart_reason(snapshot.restart_reason) ||
        snapshot.consecutive_failures > kMaximumRuntimeFailureCount) {
        return false;
    }

    const bool history_valid = has_runtime_telemetry_flag(
        snapshot, RuntimeTelemetryHistoryValid);
    const bool automatic = has_runtime_telemetry_flag(
        snapshot, RuntimeTelemetryAutomatic);
    const bool circuit_open = has_runtime_telemetry_flag(
        snapshot, RuntimeTelemetryCircuitOpen);

    if (!history_valid) {
        return snapshot.flags == RuntimeTelemetryNone &&
               snapshot.last_exit_code == kRuntimeExitCodeUnavailable &&
               snapshot.restart_reason == RuntimeRestartReason::None &&
               snapshot.consecutive_failures == 0U &&
               snapshot.event_filetime_utc == 0U;
    }

    if (snapshot.restart_reason == RuntimeRestartReason::None ||
        snapshot.event_filetime_utc == 0U ||
        circuit_open !=
            (snapshot.consecutive_failures == kMaximumRuntimeFailureCount)) {
        return false;
    }

    if (snapshot.restart_reason == RuntimeRestartReason::ManualRestart) {
        return !automatic && !circuit_open &&
               snapshot.consecutive_failures == 0U &&
               snapshot.last_exit_code == kRuntimeExitCodeUnavailable;
    }

    return automatic;
}

[[nodiscard]] constexpr RuntimeRestartReason
runtime_restart_reason_from_exit_code(std::uint32_t exit_code) noexcept
{
    switch (exit_code) {
    case 2U:
        return RuntimeRestartReason::WebViewFailure;
    case 10U:
        return RuntimeRestartReason::NavigationFailure;
    case 11U:
        return RuntimeRestartReason::CaptureExclusionFailure;
    case 13U:
        return RuntimeRestartReason::ReadySignalFailure;
    case 18U:
        return RuntimeRestartReason::PlacementFailure;
    case 19U:
        return RuntimeRestartReason::PageHealthTimeout;
    case 20U:
        return RuntimeRestartReason::ConnectionRecovery;
    case 21U:
        return RuntimeRestartReason::SystemLifecycleRecovery;
    default:
        return RuntimeRestartReason::UnexpectedExit;
    }
}

static_assert(is_valid_runtime_telemetry(RuntimeTelemetrySnapshot{}));
static_assert(
    runtime_restart_reason_from_exit_code(19U) ==
    RuntimeRestartReason::PageHealthTimeout);

} // namespace chatview
''',
    encoding="utf-8",
    newline="\n",
)

replace_once(
    "src/common/runtime-history-store.cpp",
    "checksum_begin + sizeof(RuntimeHistoryRecord::checksum);",
    "checksum_begin + sizeof(std::uint32_t);",
)
replace_once(
    "tests/runtime-telemetry-test.cpp",
    "#include <iostream>\n",
    "#include <iostream>\n#include <utility>\n",
)

Path("src/plugin/restart-policy.hpp").write_text(
    r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/runtime-telemetry.hpp"

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kMaximumAutomaticRestartFailures =
    kMaximumRuntimeFailureCount;
inline constexpr std::uint32_t kInitialAutomaticRestartDelayMs = 500U;
inline constexpr std::uint32_t kMaximumAutomaticRestartDelayMs = 30000U;

class RestartPolicy final {
public:
    constexpr void record_failure() noexcept
    {
        if (failure_count_ < kMaximumAutomaticRestartFailures) {
            ++failure_count_;
        }
    }

    constexpr void restore(std::uint32_t failure_count) noexcept
    {
        failure_count_ =
            failure_count > kMaximumAutomaticRestartFailures
                ? kMaximumAutomaticRestartFailures
                : failure_count;
    }

    constexpr void reset() noexcept
    {
        failure_count_ = 0U;
    }

    [[nodiscard]] constexpr bool automatic_restart_allowed() const noexcept
    {
        return failure_count_ < kMaximumAutomaticRestartFailures;
    }

    [[nodiscard]] constexpr std::uint32_t failure_count() const noexcept
    {
        return failure_count_;
    }

    [[nodiscard]] constexpr std::uint32_t delay_ms() const noexcept
    {
        if (failure_count_ == 0U || !automatic_restart_allowed()) {
            return 0U;
        }

        std::uint32_t delay = kInitialAutomaticRestartDelayMs;
        for (std::uint32_t index = 1U; index < failure_count_; ++index) {
            if (delay >= kMaximumAutomaticRestartDelayMs / 2U) {
                return kMaximumAutomaticRestartDelayMs;
            }
            delay *= 2U;
        }
        return delay > kMaximumAutomaticRestartDelayMs
                   ? kMaximumAutomaticRestartDelayMs
                   : delay;
    }

private:
    std::uint32_t failure_count_ = 0U;
};

static_assert(RestartPolicy{}.automatic_restart_allowed());
static_assert(RestartPolicy{}.failure_count() == 0U);
static_assert(RestartPolicy{}.delay_ms() == 0U);

} // namespace chatview
''',
    encoding="utf-8",
    newline="\n",
)

Path("tests/restart-policy-test.cpp").write_text(
    r'''// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/restart-policy.hpp"

#include <array>
#include <iostream>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    chatview::RestartPolicy policy;
    if (!policy.automatic_restart_allowed() ||
        policy.failure_count() != 0U ||
        policy.delay_ms() != 0U) {
        return fail("A new restart policy was not immediately usable");
    }

    constexpr std::array<std::uint32_t, 5U> expected_delays{
        500U,
        1000U,
        2000U,
        4000U,
        8000U,
    };

    for (std::size_t index = 0U; index < expected_delays.size(); ++index) {
        policy.record_failure();
        if (!policy.automatic_restart_allowed() ||
            policy.failure_count() != index + 1U ||
            policy.delay_ms() != expected_delays[index]) {
            return fail("Automatic restart backoff changed unexpectedly");
        }
    }

    policy.record_failure();
    if (policy.automatic_restart_allowed() ||
        policy.failure_count() !=
            chatview::kMaximumAutomaticRestartFailures ||
        policy.delay_ms() != 0U) {
        return fail("The automatic restart circuit did not open at its limit");
    }

    policy.record_failure();
    if (policy.failure_count() !=
        chatview::kMaximumAutomaticRestartFailures) {
        return fail("The restart failure counter did not saturate");
    }

    policy.reset();
    policy.restore(3U);
    if (!policy.automatic_restart_allowed() ||
        policy.failure_count() != 3U || policy.delay_ms() != 2000U) {
        return fail("A persisted restart count was not restored");
    }

    policy.restore(999U);
    if (policy.automatic_restart_allowed() ||
        policy.failure_count() !=
            chatview::kMaximumAutomaticRestartFailures) {
        return fail("A restored restart count did not saturate safely");
    }

    policy.reset();
    if (!policy.automatic_restart_allowed() ||
        policy.failure_count() != 0U ||
        policy.delay_ms() != 0U) {
        return fail("An explicit reset did not close the restart circuit");
    }

    return 0;
}
''',
    encoding="utf-8",
    newline="\n",
)

Path("src/common/control-status.hpp").write_text(
    r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/runtime-telemetry.hpp"

#include <Windows.h>

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kControlStatusMagic = 0x43565354U; // "CVST"
inline constexpr std::uint32_t kControlStatusVersion = 2U;

enum ControlStatusFlag : std::uint32_t {
    ControlStatusNone = 0U,
    ControlStatusStreaming = 1U << 0U,
    ControlStatusRecording = 1U << 1U,
    ControlStatusReplayBuffer = 1U << 2U,
    ControlStatusVirtualCamera = 1U << 3U,
    ControlStatusCaptureRisk = 1U << 4U,
    ControlStatusHudRunning = 1U << 5U,
    ControlStatusHudVisible = 1U << 6U,
};

inline constexpr std::uint32_t kKnownControlStatusFlags =
    ControlStatusStreaming | ControlStatusRecording |
    ControlStatusReplayBuffer | ControlStatusVirtualCamera |
    ControlStatusCaptureRisk | ControlStatusHudRunning |
    ControlStatusHudVisible;

[[nodiscard]] constexpr bool are_valid_control_status_flags(
    std::uint32_t flags) noexcept
{
    return (flags & ~kKnownControlStatusFlags) == 0U;
}

struct ControlStatus {
    std::uint32_t magic;
    std::uint32_t version;
    volatile LONG sequence;
    std::uint32_t flags;
    std::uint32_t hud_process_id;
    std::uint32_t runtime_telemetry_flags;
    std::uint64_t generation;
    std::uint64_t updated_tick_ms;
    std::uint64_t runtime_event_filetime_utc;
    std::uint32_t last_hud_exit_code;
    std::uint32_t runtime_restart_reason;
    std::uint32_t consecutive_runtime_failures;
    std::uint32_t reserved32;
};

struct ControlStatusSnapshot {
    std::uint32_t flags = ControlStatusNone;
    std::uint32_t hud_process_id = 0U;
    std::uint64_t generation = 0U;
    std::uint64_t updated_tick_ms = 0U;
    RuntimeTelemetrySnapshot runtime_telemetry;
};

[[nodiscard]] constexpr bool has_control_status_flag(
    const ControlStatusSnapshot &snapshot,
    ControlStatusFlag flag) noexcept
{
    return (snapshot.flags & static_cast<std::uint32_t>(flag)) != 0U;
}

[[nodiscard]] constexpr bool is_valid_control_status_snapshot(
    const ControlStatusSnapshot &snapshot) noexcept
{
    if (!are_valid_control_status_flags(snapshot.flags) ||
        !is_valid_runtime_telemetry(snapshot.runtime_telemetry)) {
        return false;
    }

    const bool running = has_control_status_flag(
        snapshot, ControlStatusHudRunning);
    const bool visible = has_control_status_flag(
        snapshot, ControlStatusHudVisible);
    if (visible && !running) {
        return false;
    }
    return running == (snapshot.hud_process_id != 0U);
}

static_assert(sizeof(LONG) == sizeof(std::int32_t));
static_assert(sizeof(ControlStatus) == 64U);
static_assert(are_valid_control_status_flags(ControlStatusNone));
static_assert(are_valid_control_status_flags(kKnownControlStatusFlags));
static_assert(!are_valid_control_status_flags(1U << 7U));

} // namespace chatview
''',
    encoding="utf-8",
    newline="\n",
)

Path("src/plugin/runtime-controller.hpp").write_text(
    r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/runtime-history-store.hpp"
#include "common/shared-state.hpp"
#include "common/srw-lock.hpp"
#include "common/win32-handle.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace chatview {

class RuntimeController final {
public:
    RuntimeController() = default;
    ~RuntimeController();

    RuntimeController(const RuntimeController &) = delete;
    RuntimeController &operator=(const RuntimeController &) = delete;

    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;
    void update(
        bool streaming, bool recording, bool capture_risk) noexcept;
    [[nodiscard]] RuntimeTelemetrySnapshot runtime_telemetry() const noexcept;
    [[nodiscard]] bool restart_hud() noexcept;
    [[nodiscard]] bool toggle_edit_mode() noexcept;

private:
    struct RuntimeLaunchResult {
        bool started = false;
        RuntimeRestartReason failure_reason =
            RuntimeRestartReason::LaunchFailure;
        std::uint32_t exit_code = kRuntimeExitCodeUnavailable;
    };

    [[nodiscard]] bool create_transport_locked();
    [[nodiscard]] bool create_runtime_job_locked();
    [[nodiscard]] RuntimeLaunchResult launch_runtime_locked();
    [[nodiscard]] HWND find_runtime_window_locked() const noexcept;
    [[nodiscard]] std::wstring find_sibling_path(const wchar_t *file_name) const;

    void supervisor_loop() noexcept;
    void update_runtime_telemetry(
        const RuntimeTelemetrySnapshot &snapshot) noexcept;
    void publish_locked(std::uint32_t flags) noexcept;
    void terminate_runtime_locked(const char *reason) noexcept;
    void cleanup_locked() noexcept;

    std::mutex mutex_;
    SRWLOCK state_publish_lock_ = SRWLOCK_INIT;
    mutable SRWLOCK runtime_telemetry_lock_ = SRWLOCK_INIT;
    std::thread supervisor_thread_;
    std::atomic_bool stopping_{true};
    std::atomic_bool restart_requested_{false};
    std::atomic<std::uint32_t> current_flags_{SharedStateNone};
    HANDLE state_publish_handle_ = nullptr;
    UniqueHandle supervisor_stop_event_;
    UniqueHandle state_publish_event_;
    UniqueHandle runtime_job_;
    UniqueHandle mapping_;
    UniqueHandle state_changed_event_;
    UniqueHandle runtime_ready_event_;
    UniqueHandle runtime_process_;
    SharedState *shared_state_ = nullptr;
    std::wstring mapping_name_;
    std::wstring event_name_;
    std::wstring ready_event_name_;
    DWORD runtime_process_id_ = 0U;
    std::uint64_t generation_ = 0U;
    RuntimeTelemetrySnapshot runtime_telemetry_state_;
    RuntimeHistoryStore runtime_history_store_;
};

} // namespace chatview
''',
    encoding="utf-8",
    newline="\n",
)

Path("src/plugin/control-center-bridge.hpp").write_text(
    r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/control-status.hpp"
#include "common/srw-lock.hpp"
#include "common/win32-handle.hpp"

#include <Windows.h>

#include <cstdint>
#include <string>

namespace chatview {

class ControlCenterBridge final {
public:
    ControlCenterBridge() = default;
    ~ControlCenterBridge();

    ControlCenterBridge(const ControlCenterBridge &) = delete;
    ControlCenterBridge &operator=(const ControlCenterBridge &) = delete;

    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;

    void update(
        bool streaming,
        bool recording,
        bool replay_buffer,
        bool virtual_camera,
        bool capture_risk,
        const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept;

    [[nodiscard]] bool open_control_center() noexcept;
    [[nodiscard]] bool consume_restart_request() noexcept;

private:
    [[nodiscard]] std::wstring find_sibling_path(
        const wchar_t *file_name) const;
    void publish_locked(
        std::uint32_t flags,
        DWORD hud_process_id,
        const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept;

    SRWLOCK lock_ = SRWLOCK_INIT;
    UniqueHandle mapping_;
    UniqueHandle status_changed_event_;
    UniqueHandle restart_event_;
    ControlStatus *status_ = nullptr;
    std::wstring mapping_name_;
    std::wstring status_event_name_;
    std::wstring restart_event_name_;
    std::uint32_t last_flags_ = ControlStatusNone;
    DWORD last_hud_process_id_ = 0U;
    RuntimeTelemetrySnapshot last_runtime_telemetry_;
    std::uint64_t generation_ = 0U;
};

} // namespace chatview
''',
    encoding="utf-8",
    newline="\n",
)

Path("tests/control-status-reader-test.cpp").write_text(
    r'''// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/control-status.hpp"
#include "common/win32-handle.hpp"
#include "config/control-status-reader.hpp"

#include <Windows.h>

#include <iostream>
#include <string>

namespace {

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

int fail(const wchar_t *message)
{
    std::wcerr << message << L'\n';
    return 1;
}

void publish(
    chatview::ControlStatus *status,
    std::uint32_t flags,
    DWORD hud_process_id,
    std::uint64_t generation,
    const chatview::RuntimeTelemetrySnapshot &runtime) noexcept
{
    InterlockedIncrement(&status->sequence);
    MemoryBarrier();
    status->flags = flags;
    status->hud_process_id = hud_process_id;
    status->runtime_telemetry_flags = runtime.flags;
    status->generation = generation;
    status->updated_tick_ms = GetTickCount64();
    status->runtime_event_filetime_utc = runtime.event_filetime_utc;
    status->last_hud_exit_code = runtime.last_exit_code;
    status->runtime_restart_reason =
        static_cast<std::uint32_t>(runtime.restart_reason);
    status->consecutive_runtime_failures = runtime.consecutive_failures;
    MemoryBarrier();
    InterlockedIncrement(&status->sequence);
}

} // namespace

int main()
{
    const DWORD process_id = GetCurrentProcessId();
    const std::wstring suffix = std::to_wstring(process_id);
    const std::wstring mapping_name =
        L"Local\\ChatViewOBS.Test.ControlStatus." + suffix;
    const std::wstring status_event_name =
        L"Local\\ChatViewOBS.Test.ControlChanged." + suffix;
    const std::wstring restart_event_name =
        L"Local\\ChatViewOBS.Test.ControlRestart." + suffix;

    chatview::UniqueHandle mapping(CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0U,
        static_cast<DWORD>(sizeof(chatview::ControlStatus)),
        mapping_name.c_str()));
    if (!mapping) {
        return fail(L"Failed to create the control-status mapping");
    }

    MappedStatus mapped(static_cast<chatview::ControlStatus *>(
        MapViewOfFile(
            mapping.get(),
            FILE_MAP_ALL_ACCESS,
            0U,
            0U,
            sizeof(chatview::ControlStatus))));
    if (mapped.get() == nullptr) {
        return fail(L"Failed to map the control-status fixture");
    }

    chatview::UniqueHandle status_event(CreateEventW(
        nullptr, FALSE, FALSE, status_event_name.c_str()));
    chatview::UniqueHandle restart_event(CreateEventW(
        nullptr, FALSE, FALSE, restart_event_name.c_str()));
    if (!status_event || !restart_event) {
        return fail(L"Failed to create control-status events");
    }

    ZeroMemory(mapped.get(), sizeof(chatview::ControlStatus));
    mapped.get()->magic = chatview::kControlStatusMagic;
    mapped.get()->version = chatview::kControlStatusVersion;

    chatview::ControlStatusReader reader;
    if (!reader.open(
            mapping_name,
            status_event_name,
            restart_event_name,
            process_id)) {
        return fail(L"The control-status reader could not connect");
    }
    if (!reader.parent_alive()) {
        return fail(L"The reader did not observe its live parent process");
    }

    const std::uint32_t flags =
        chatview::ControlStatusStreaming |
        chatview::ControlStatusCaptureRisk |
        chatview::ControlStatusHudRunning;
    const chatview::RuntimeTelemetrySnapshot telemetry{
        19U,
        chatview::RuntimeRestartReason::PageHealthTimeout,
        2U,
        chatview::RuntimeTelemetryHistoryValid |
            chatview::RuntimeTelemetryAutomatic,
        133485408000000000ULL,
    };
    publish(mapped.get(), flags, process_id, 7U, telemetry);
    SetEvent(status_event.get());

    chatview::ControlStatusSnapshot snapshot;
    if (!reader.read(snapshot) || snapshot.flags != flags ||
        snapshot.hud_process_id != process_id || snapshot.generation != 7U ||
        snapshot.updated_tick_ms == 0U ||
        snapshot.runtime_telemetry != telemetry) {
        return fail(L"The control-status snapshot did not round trip");
    }

    if (!reader.request_restart() ||
        WaitForSingleObject(restart_event.get(), 1000U) != WAIT_OBJECT_0) {
        return fail(L"The restart command was not delivered");
    }

    publish(mapped.get(), 1U << 31U, 0U, 8U, {});
    if (reader.read(snapshot)) {
        return fail(L"Unknown control-status flags were accepted");
    }

    publish(
        mapped.get(), chatview::ControlStatusHudVisible, 0U, 9U, {});
    if (reader.read(snapshot)) {
        return fail(L"A visible HUD without a running process was accepted");
    }

    chatview::RuntimeTelemetrySnapshot invalid = telemetry;
    invalid.flags |= chatview::RuntimeTelemetryCircuitOpen;
    publish(mapped.get(), chatview::ControlStatusNone, 0U, 10U, invalid);
    if (reader.read(snapshot)) {
        return fail(L"Invalid runtime telemetry was accepted");
    }

    reader.close();
    return 0;
}
''',
    encoding="utf-8",
    newline="\n",
)

replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.3.8 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.3.9 LANGUAGES CXX)",
)
replace_once(
    "CMakeLists.txt",
    '''chatview_enable_win32(chat-view-diagnostic-redaction)
chatview_enable_warnings(chat-view-diagnostic-redaction)

add_library(chat-view-obs MODULE
''',
    '''chatview_enable_win32(chat-view-diagnostic-redaction)
chatview_enable_warnings(chat-view-diagnostic-redaction)

add_library(chat-view-runtime-history STATIC
    src/common/runtime-history-store.cpp
    src/common/runtime-history-store.hpp
    src/common/runtime-telemetry.hpp
)
target_include_directories(
    chat-view-runtime-history PUBLIC "${CHATVIEW_SOURCE_DIR}")
chatview_enable_win32(chat-view-runtime-history)
chatview_enable_warnings(chat-view-runtime-history)

add_library(chat-view-obs MODULE
''',
)
replace_once(
    "CMakeLists.txt",
    '''target_link_libraries(
    chat-view-obs PRIVATE OBS::libobs OBS::obs-frontend-api bcrypt)
''',
    '''target_link_libraries(
    chat-view-obs PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    chat-view-runtime-history
    bcrypt
)
''',
)
replace_once(
    "CMakeLists.txt",
    '''target_link_libraries(chat-view-diagnostics-core PUBLIC
    chat-view-common
    chat-view-diagnostic-redaction
    bcrypt
''',
    '''target_link_libraries(chat-view-diagnostics-core PUBLIC
    chat-view-common
    chat-view-diagnostic-redaction
    chat-view-runtime-history
    bcrypt
''',
)
replace_once(
    "CMakeLists.txt",
    '''    add_test(
        NAME chat-view-diagnostic-redaction
        COMMAND chat-view-diagnostic-redaction-test
    )

    add_executable(chat-view-diagnostics-exporter-test
''',
    '''    add_test(
        NAME chat-view-diagnostic-redaction
        COMMAND chat-view-diagnostic-redaction-test
    )

    add_executable(chat-view-runtime-telemetry-test
        tests/runtime-telemetry-test.cpp
    )
    target_include_directories(
        chat-view-runtime-telemetry-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-runtime-telemetry-test)
    add_test(
        NAME chat-view-runtime-telemetry
        COMMAND chat-view-runtime-telemetry-test
    )

    add_executable(chat-view-runtime-history-store-test
        tests/runtime-history-store-test.cpp
    )
    target_link_libraries(
        chat-view-runtime-history-store-test PRIVATE
        chat-view-runtime-history)
    chatview_enable_win32(chat-view-runtime-history-store-test)
    chatview_enable_warnings(chat-view-runtime-history-store-test)
    add_test(
        NAME chat-view-runtime-history-store
        COMMAND chat-view-runtime-history-store-test
    )

    add_executable(chat-view-diagnostics-exporter-test
''',
)

replace_once(
    "src/plugin/runtime-controller.cpp",
    '''void log_process_exit(HANDLE process, const char *context) noexcept
{
    DWORD exit_code = 0U;
    if (GetExitCodeProcess(process, &exit_code)) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD runtime %s with exit code %lu",
            context,
            static_cast<unsigned long>(exit_code));
    } else {
        log_windows_error("GetExitCodeProcess", GetLastError());
    }
}
''',
    '''std::uint32_t log_process_exit(
    HANDLE process, const char *context) noexcept
{
    DWORD exit_code = kRuntimeExitCodeUnavailable;
    if (GetExitCodeProcess(process, &exit_code)) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD runtime %s with exit code %lu",
            context,
            static_cast<unsigned long>(exit_code));
        return exit_code;
    }

    log_windows_error("GetExitCodeProcess", GetLastError());
    return kRuntimeExitCodeUnavailable;
}

std::uint64_t current_filetime_utc() noexcept
{
    FILETIME filetime{};
    GetSystemTimeAsFileTime(&filetime);
    ULARGE_INTEGER value{};
    value.LowPart = filetime.dwLowDateTime;
    value.HighPart = filetime.dwHighDateTime;
    return value.QuadPart;
}
''',
)
replace_once(
    "src/plugin/runtime-controller.cpp",
    '''bool RuntimeController::start() noexcept
{
    stop();

    try {
''',
    '''bool RuntimeController::start() noexcept
{
    stop();

    RuntimeTelemetrySnapshot restored_history;
    const bool history_loaded =
        runtime_history_store_.load(restored_history);
    {
        ExclusiveSrwLockGuard telemetry_lock(runtime_telemetry_lock_);
        runtime_telemetry_state_ =
            history_loaded ? restored_history : RuntimeTelemetrySnapshot{};
    }

    try {
''',
)
replace_once(
    "src/plugin/runtime-controller.cpp",
    '''void RuntimeController::update(
    bool streaming, bool recording, bool capture_risk) noexcept
{
''',
    '''void RuntimeController::update(
    bool streaming, bool recording, bool capture_risk) noexcept
{
''',
)
replace_once(
    "src/plugin/runtime-controller.cpp",
    '''    if (state_publish_handle_ != nullptr && !SetEvent(state_publish_handle_)) {
        log_windows_error("SetEvent(state publish)", GetLastError());
    }
}


bool RuntimeController::restart_hud() noexcept
''',
    '''    if (state_publish_handle_ != nullptr && !SetEvent(state_publish_handle_)) {
        log_windows_error("SetEvent(state publish)", GetLastError());
    }
}

RuntimeTelemetrySnapshot RuntimeController::runtime_telemetry() const noexcept
{
    SharedSrwLockGuard telemetry_lock(runtime_telemetry_lock_);
    return runtime_telemetry_state_;
}

bool RuntimeController::restart_hud() noexcept
''',
)

replace_between(
    "src/plugin/runtime-controller.cpp",
    "bool RuntimeController::launch_runtime_locked()\n",
    "HWND RuntimeController::find_runtime_window_locked() const noexcept\n",
    r'''RuntimeController::RuntimeLaunchResult
RuntimeController::launch_runtime_locked()
{
    RuntimeLaunchResult result;
    const std::wstring runtime_path =
        find_sibling_path(kRuntimeExecutableName);
    if (runtime_path.empty() || !is_regular_file(runtime_path)) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD runtime was not found: %ls",
            runtime_path.c_str());
        return result;
    }

    if (!ResetEvent(runtime_ready_event_.get())) {
        log_windows_error("ResetEvent(runtime ready)", GetLastError());
        return result;
    }

    if (!create_runtime_job_locked()) {
        return result;
    }

    const DWORD process_id = GetCurrentProcessId();
    std::wstring command_line =
        L"\"" + runtime_path + L"\" --mapping \"" + mapping_name_ +
        L"\" --event \"" + event_name_ + L"\" --ready-event \"" +
        ready_event_name_ + L"\" --parent " +
        std::to_wstring(process_id);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(
            runtime_path.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
            nullptr,
            nullptr,
            &startup_info,
            &process_info)) {
        log_windows_error("CreateProcessW(HUD)", GetLastError());
        runtime_job_.reset();
        return result;
    }

    UniqueHandle thread(process_info.hThread);
    UniqueHandle process(process_info.hProcess);

    if (!AssignProcessToJobObject(runtime_job_.get(), process.get())) {
        log_windows_error("AssignProcessToJobObject(HUD)", GetLastError());
        TerminateProcess(process.get(), 1U);
        WaitForSingleObject(process.get(), kRuntimeTerminateWaitMs);
        runtime_job_.reset();
        return result;
    }

    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        log_windows_error("ResumeThread(HUD)", GetLastError());
        TerminateProcess(process.get(), 1U);
        WaitForSingleObject(process.get(), kRuntimeTerminateWaitMs);
        runtime_job_.reset();
        return result;
    }

    HANDLE wait_handles[3] = {
        runtime_ready_event_.get(),
        process.get(),
        supervisor_stop_event_.get(),
    };
    const DWORD wait_result =
        WaitForMultipleObjects(3U, wait_handles, FALSE, kRuntimeReadyWaitMs);
    if (wait_result == WAIT_OBJECT_0 + 1U) {
        result.exit_code =
            log_process_exit(process.get(), "exited before reporting ready");
        result.failure_reason =
            runtime_restart_reason_from_exit_code(result.exit_code);
        runtime_job_.reset();
        return result;
    }
    if (wait_result == WAIT_OBJECT_0 + 2U) {
        result.failure_reason = RuntimeRestartReason::None;
        TerminateProcess(process.get(), 0U);
        WaitForSingleObject(process.get(), kRuntimeTerminateWaitMs);
        runtime_job_.reset();
        return result;
    }
    if (wait_result != WAIT_OBJECT_0) {
        if (wait_result == WAIT_FAILED) {
            log_windows_error(
                "WaitForMultipleObjects(HUD ready)", GetLastError());
        } else {
            result.failure_reason = RuntimeRestartReason::StartupTimeout;
            blog(
                LOG_ERROR,
                "[ChatView OBS] HUD did not report ready within %lu ms",
                static_cast<unsigned long>(kRuntimeReadyWaitMs));
        }

        if (WaitForSingleObject(process.get(), 0U) == WAIT_TIMEOUT) {
            TerminateProcess(process.get(), 1U);
            WaitForSingleObject(process.get(), kRuntimeTerminateWaitMs);
        }
        runtime_job_.reset();
        return result;
    }

    runtime_process_id_ = process_info.dwProcessId;
    runtime_process_ = std::move(process);
    result.started = true;
    result.failure_reason = RuntimeRestartReason::None;
    return result;
}

''',
)

replace_between(
    "src/plugin/runtime-controller.cpp",
    "void RuntimeController::supervisor_loop() noexcept\n",
    "void RuntimeController::publish_locked(std::uint32_t flags) noexcept\n",
    r'''void RuntimeController::supervisor_loop() noexcept
{
    RestartPolicy restart_policy;
    const RuntimeTelemetrySnapshot restored = runtime_telemetry();
    if (has_runtime_telemetry_flag(
            restored, RuntimeTelemetryHistoryValid)) {
        restart_policy.restore(restored.consecutive_failures);
        if (!restart_policy.automatic_restart_allowed()) {
            blog(
                LOG_WARNING,
                "[ChatView OBS] HUD automatic restart circuit restored open; "
                "use Tools > Restart ChatView HUD to retry");
        }
    }

    ULONGLONG retry_deadline = 0U;

    const auto record_failure =
        [this, &restart_policy, &retry_deadline](
            RuntimeRestartReason reason,
            std::uint32_t exit_code) noexcept {
            if (reason == RuntimeRestartReason::None) {
                reason = RuntimeRestartReason::LaunchFailure;
            }

            restart_policy.record_failure();
            std::uint32_t telemetry_flags =
                RuntimeTelemetryHistoryValid |
                RuntimeTelemetryAutomatic;
            if (!restart_policy.automatic_restart_allowed()) {
                telemetry_flags |= RuntimeTelemetryCircuitOpen;
            }
            update_runtime_telemetry(RuntimeTelemetrySnapshot{
                exit_code,
                reason,
                restart_policy.failure_count(),
                telemetry_flags,
                current_filetime_utc(),
            });

            if (!restart_policy.automatic_restart_allowed()) {
                retry_deadline = 0U;
                blog(
                    LOG_ERROR,
                    "[ChatView OBS] HUD automatic restart disabled after %u "
                    "consecutive failures; use Tools > Restart ChatView HUD",
                    static_cast<unsigned int>(restart_policy.failure_count()));
                return;
            }

            const DWORD delay =
                static_cast<DWORD>(restart_policy.delay_ms());
            retry_deadline = GetTickCount64() + delay;
            blog(
                LOG_WARNING,
                "[ChatView OBS] HUD restart scheduled in %lu ms after failure "
                "%u/%u",
                static_cast<unsigned long>(delay),
                static_cast<unsigned int>(restart_policy.failure_count()),
                static_cast<unsigned int>(
                    kMaximumAutomaticRestartFailures));
        };

    const auto record_manual_restart =
        [this, &restart_policy, &retry_deadline]() noexcept {
            restart_policy.reset();
            retry_deadline = 0U;
            update_runtime_telemetry(RuntimeTelemetrySnapshot{
                kRuntimeExitCodeUnavailable,
                RuntimeRestartReason::ManualRestart,
                0U,
                RuntimeTelemetryHistoryValid,
                current_filetime_utc(),
            });
        };

    const auto clear_stable_failure_count =
        [this, &restart_policy, &retry_deadline]() noexcept {
            if (restart_policy.failure_count() == 0U) {
                return;
            }

            restart_policy.reset();
            retry_deadline = 0U;
            RuntimeTelemetrySnapshot telemetry = runtime_telemetry();
            if (!has_runtime_telemetry_flag(
                    telemetry, RuntimeTelemetryHistoryValid)) {
                return;
            }
            telemetry.consecutive_failures = 0U;
            telemetry.flags &= ~static_cast<std::uint32_t>(
                RuntimeTelemetryCircuitOpen);
            update_runtime_telemetry(telemetry);
        };

    try {
        while (!stopping_.load(std::memory_order_acquire)) {
            HANDLE stop_event = nullptr;
            HANDLE publish_event = nullptr;
            HANDLE process = nullptr;
            DWORD process_id = 0U;
            {
                std::scoped_lock lock(mutex_);
                stop_event = supervisor_stop_event_.get();
                publish_event = state_publish_event_.get();
                process = runtime_process_.get();
                process_id = runtime_process_id_;
            }

            if (stop_event == nullptr || publish_event == nullptr) {
                return;
            }

            if (process == nullptr) {
                DWORD wait_timeout = INFINITE;
                if (restart_policy.automatic_restart_allowed()) {
                    const ULONGLONG now = GetTickCount64();
                    const ULONGLONG remaining =
                        retry_deadline > now ? retry_deadline - now : 0U;
                    wait_timeout =
                        remaining >= static_cast<ULONGLONG>(INFINITE)
                            ? INFINITE - 1U
                            : static_cast<DWORD>(remaining);
                }

                HANDLE wait_handles[2] = {stop_event, publish_event};
                const DWORD wait_result = WaitForMultipleObjects(
                    2U, wait_handles, FALSE, wait_timeout);
                if (wait_result == WAIT_OBJECT_0) {
                    return;
                }
                if (wait_result == WAIT_OBJECT_0 + 1U) {
                    const bool explicit_restart =
                        restart_requested_.exchange(
                            false, std::memory_order_acq_rel);
                    if (!explicit_restart) {
                        continue;
                    }

                    record_manual_restart();
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] Manual HUD restart requested");
                } else if (wait_result == WAIT_FAILED) {
                    log_windows_error(
                        "WaitForMultipleObjects(HUD restart backoff)",
                        GetLastError());
                    return;
                } else if (wait_result != WAIT_TIMEOUT) {
                    blog(
                        LOG_ERROR,
                        "[ChatView OBS] HUD restart wait returned unexpected "
                        "result %lu",
                        static_cast<unsigned long>(wait_result));
                    return;
                }

                if (!restart_policy.automatic_restart_allowed()) {
                    continue;
                }

                RuntimeLaunchResult launch_result;
                {
                    std::scoped_lock lock(mutex_);
                    if (stopping_.load(std::memory_order_acquire)) {
                        return;
                    }
                    launch_result = launch_runtime_locked();
                    if (launch_result.started) {
                        publish_locked(
                            current_flags_.load(
                                std::memory_order_acquire));
                    }
                }

                if (stopping_.load(std::memory_order_acquire)) {
                    return;
                }
                if (launch_result.started) {
                    retry_deadline = 0U;
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] HUD runtime started and reported "
                        "ready");
                } else if (
                    launch_result.failure_reason !=
                    RuntimeRestartReason::None) {
                    record_failure(
                        launch_result.failure_reason,
                        launch_result.exit_code);
                }
                continue;
            }

            HANDLE wait_handles[3] = {
                stop_event,
                process,
                publish_event,
            };
            const DWORD wait_result = WaitForMultipleObjects(
                3U,
                wait_handles,
                FALSE,
                kRuntimeStablePeriodMs);
            if (wait_result == WAIT_OBJECT_0) {
                return;
            }
            if (wait_result == WAIT_OBJECT_0 + 2U) {
                const bool explicit_restart =
                    restart_requested_.exchange(
                        false, std::memory_order_acq_rel);
                bool manual_restart_accepted = false;

                {
                    std::scoped_lock lock(mutex_);
                    if (runtime_process_id_ != process_id ||
                        runtime_process_.get() != process) {
                        continue;
                    }

                    if (explicit_restart) {
                        if (WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
                            terminate_runtime_locked(
                                "was restarted from the OBS Tools menu");
                        } else {
                            runtime_process_.reset();
                            runtime_process_id_ = 0U;
                            runtime_job_.reset();
                        }
                        manual_restart_accepted = true;
                    } else if (
                        WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
                        publish_locked(
                            current_flags_.load(
                                std::memory_order_acquire));
                    }
                }

                if (manual_restart_accepted) {
                    record_manual_restart();
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] Manual HUD restart accepted");
                }
                continue;
            }
            if (wait_result == WAIT_TIMEOUT) {
                if (restart_policy.failure_count() != 0U) {
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] HUD remained stable; restart "
                        "failure counter reset");
                }
                clear_stable_failure_count();
                continue;
            }
            if (wait_result == WAIT_FAILED) {
                log_windows_error(
                    "WaitForMultipleObjects(HUD supervisor)",
                    GetLastError());
                return;
            }
            if (wait_result != WAIT_OBJECT_0 + 1U) {
                blog(
                    LOG_ERROR,
                    "[ChatView OBS] HUD supervisor received unexpected "
                    "wait result %lu",
                    static_cast<unsigned long>(wait_result));
                return;
            }

            bool exited = false;
            std::uint32_t exit_code = kRuntimeExitCodeUnavailable;
            {
                std::scoped_lock lock(mutex_);
                if (runtime_process_id_ == process_id &&
                    runtime_process_.get() == process) {
                    exit_code = log_process_exit(
                        runtime_process_.get(), "exited unexpectedly");
                    runtime_process_.reset();
                    runtime_process_id_ = 0U;
                    runtime_job_.reset();
                    exited = true;
                }
            }
            if (exited) {
                record_failure(
                    runtime_restart_reason_from_exit_code(exit_code),
                    exit_code);
            }
        }
    } catch (const std::exception &error) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD supervisor failed: %s",
            error.what());
    } catch (...) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD supervisor failed with an unknown "
            "exception");
    }

    stopping_.store(true, std::memory_order_release);
    try {
        std::scoped_lock lock(mutex_);
        terminate_runtime_locked("lost its supervisor");
    } catch (...) {
        if (runtime_job_) {
            TerminateJobObject(runtime_job_.get(), 1U);
        }
    }
}

void RuntimeController::update_runtime_telemetry(
    const RuntimeTelemetrySnapshot &snapshot) noexcept
{
    if (!is_valid_runtime_telemetry(snapshot)) {
        blog(LOG_ERROR, "[ChatView OBS] Invalid HUD runtime telemetry rejected");
        return;
    }

    {
        ExclusiveSrwLockGuard telemetry_lock(runtime_telemetry_lock_);
        if (runtime_telemetry_state_ == snapshot) {
            return;
        }
        runtime_telemetry_state_ = snapshot;
    }

    if (!runtime_history_store_.save(snapshot)) {
        blog(
            LOG_WARNING,
            "[ChatView OBS] HUD runtime history could not be persisted");
    }
}

''',
)

replace_once(
    "src/plugin/control-center-bridge.cpp",
    "            publish_locked(ControlStatusNone, 0U);",
    "            publish_locked(ControlStatusNone, 0U, {});",
)
replace_once(
    "src/plugin/control-center-bridge.cpp",
    '''            last_flags_ = ControlStatusNone;
            last_hud_process_id_ = 0U;
            generation_ = 0U;
''',
    '''            last_flags_ = ControlStatusNone;
            last_hud_process_id_ = 0U;
            last_runtime_telemetry_ = {};
            generation_ = 0U;
''',
)
replace_once(
    "src/plugin/control-center-bridge.cpp",
    '''    last_flags_ = ControlStatusNone;
    last_hud_process_id_ = 0U;
    generation_ = 0U;
''',
    '''    last_flags_ = ControlStatusNone;
    last_hud_process_id_ = 0U;
    last_runtime_telemetry_ = {};
    generation_ = 0U;
''',
)
replace_once(
    "src/plugin/control-center-bridge.cpp",
    '''void ControlCenterBridge::update(
    bool streaming,
    bool recording,
    bool replay_buffer,
    bool virtual_camera,
    bool capture_risk) noexcept
''',
    '''void ControlCenterBridge::update(
    bool streaming,
    bool recording,
    bool replay_buffer,
    bool virtual_camera,
    bool capture_risk,
    const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept
''',
)
replace_once(
    "src/plugin/control-center-bridge.cpp",
    '''    ExclusiveSrwLockGuard guard(lock_);
    if (status_ == nullptr ||
        (flags == last_flags_ &&
         hud.process_id == last_hud_process_id_)) {
        return;
    }
    publish_locked(flags, hud.process_id);
''',
    '''    const RuntimeTelemetrySnapshot safe_telemetry =
        is_valid_runtime_telemetry(runtime_telemetry)
            ? runtime_telemetry
            : RuntimeTelemetrySnapshot{};

    ExclusiveSrwLockGuard guard(lock_);
    if (status_ == nullptr ||
        (flags == last_flags_ &&
         hud.process_id == last_hud_process_id_ &&
         safe_telemetry == last_runtime_telemetry_)) {
        return;
    }
    publish_locked(flags, hud.process_id, safe_telemetry);
''',
)
replace_once(
    "src/plugin/control-center-bridge.cpp",
    '''void ControlCenterBridge::publish_locked(
    std::uint32_t flags, DWORD hud_process_id) noexcept
''',
    '''void ControlCenterBridge::publish_locked(
    std::uint32_t flags,
    DWORD hud_process_id,
    const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept
''',
)
replace_once(
    "src/plugin/control-center-bridge.cpp",
    '''    status_->flags = flags;
    status_->hud_process_id = hud_process_id;
    status_->generation = ++generation_;
    status_->updated_tick_ms = GetTickCount64();
''',
    '''    status_->flags = flags;
    status_->hud_process_id = hud_process_id;
    status_->runtime_telemetry_flags = runtime_telemetry.flags;
    status_->generation = ++generation_;
    status_->updated_tick_ms = GetTickCount64();
    status_->runtime_event_filetime_utc =
        runtime_telemetry.event_filetime_utc;
    status_->last_hud_exit_code = runtime_telemetry.last_exit_code;
    status_->runtime_restart_reason =
        static_cast<std::uint32_t>(runtime_telemetry.restart_reason);
    status_->consecutive_runtime_failures =
        runtime_telemetry.consecutive_failures;
''',
)
replace_once(
    "src/plugin/control-center-bridge.cpp",
    '''    last_flags_ = flags;
    last_hud_process_id_ = hud_process_id;
''',
    '''    last_flags_ = flags;
    last_hud_process_id_ = hud_process_id;
    last_runtime_telemetry_ = runtime_telemetry;
''',
)

replace_once(
    "src/plugin/plugin-main.cpp",
    '''            replay_buffer,
            virtual_camera,
            suppress);
''',
    '''            replay_buffer,
            virtual_camera,
            suppress,
            runtime_controller->runtime_telemetry());
''',
)

replace_once(
    "src/config/control-status-reader.cpp",
    '''        candidate.flags = status_->flags;
        candidate.hud_process_id = status_->hud_process_id;
        candidate.generation = status_->generation;
        candidate.updated_tick_ms = status_->updated_tick_ms;
''',
    '''        candidate.flags = status_->flags;
        candidate.hud_process_id = status_->hud_process_id;
        candidate.generation = status_->generation;
        candidate.updated_tick_ms = status_->updated_tick_ms;
        candidate.runtime_telemetry = RuntimeTelemetrySnapshot{
            status_->last_hud_exit_code,
            static_cast<RuntimeRestartReason>(
                status_->runtime_restart_reason),
            status_->consecutive_runtime_failures,
            status_->runtime_telemetry_flags,
            status_->runtime_event_filetime_utc,
        };
''',
)

replace_once(
    "README.md",
    '''- bounded shutdown and an automatic restart circuit that stops crash loops;
''',
    '''- bounded shutdown and a persistent automatic restart circuit that stops crash loops across OBS restarts;
''',
)
replace_once(
    "README.md",
    '''Use **Export diagnostics** in the Control Center, or **Tools → Export ChatView Diagnostics...** in OBS, to create a Desktop folder containing a binary-integrity summary, the current OBS/HUD state when available, and only ChatView-tagged OBS log lines. The export omits the configured URL and chat messages, redacts user-profile paths and IPC names, uploads nothing automatically, and must be reviewed before sharing.
''',
    '''Use **Export diagnostics** in the Control Center, or **Tools → Export ChatView Diagnostics...** in OBS, to create a Desktop folder containing a binary-integrity summary, the current OBS/HUD state when available, the persisted last HUD exit and restart-circuit history, and only ChatView-tagged OBS log lines. The export omits the configured URL and chat messages, redacts user-profile paths and IPC names, uploads nothing automatically, and must be reviewed before sharing.
''',
)
