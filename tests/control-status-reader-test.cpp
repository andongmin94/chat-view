// SPDX-License-Identifier: GPL-2.0-or-later

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
