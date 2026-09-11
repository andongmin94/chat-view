// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/control-center-bridge.hpp"

#include "common/window-messages.hpp"
#include "plugin/transport-token.hpp"

#include <obs-module.h>

#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <string>

namespace chatview {
namespace {

constexpr wchar_t kControlCenterExecutableName[] = L"chat-view-config.exe";
constexpr std::size_t kMaximumDirectChildren = 64U;

struct DirectChildren {
    std::array<DWORD, kMaximumDirectChildren> process_ids{};
    std::size_t size = 0U;
};

struct HudWindowSearch {
    const DirectChildren *children = nullptr;
    HWND window = nullptr;
    DWORD process_id = 0U;
};

std::wstring last_error_message(DWORD error)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0U,
        reinterpret_cast<LPWSTR>(&buffer),
        0U,
        nullptr);

    std::wstring message;
    if (length != 0U && buffer != nullptr) {
        message.assign(buffer, length);
        LocalFree(buffer);
    }
    return message;
}

void log_windows_error(const char *operation, DWORD error)
{
    const std::wstring detail = last_error_message(error);
    blog(
        LOG_ERROR,
        "[ChatView OBS] %s failed with error %lu%s%ls",
        operation,
        static_cast<unsigned long>(error),
        detail.empty() ? "" : ": ",
        detail.empty() ? L"" : detail.c_str());
}

bool is_regular_file(const std::wstring &path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

DirectChildren direct_child_processes(DWORD parent_process_id) noexcept
{
    DirectChildren children;
    const HANDLE snapshot_handle =
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U);
    if (snapshot_handle == INVALID_HANDLE_VALUE) {
        return children;
    }
    UniqueHandle snapshot(snapshot_handle);

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        return children;
    }

    do {
        if (entry.th32ParentProcessID == parent_process_id &&
            children.size < children.process_ids.size()) {
            children.process_ids[children.size++] = entry.th32ProcessID;
        }
    } while (Process32NextW(snapshot.get(), &entry));

    return children;
}

bool contains_process(
    const DirectChildren &children, DWORD process_id) noexcept
{
    for (std::size_t index = 0U; index < children.size; ++index) {
        if (children.process_ids[index] == process_id) {
            return true;
        }
    }
    return false;
}

BOOL CALLBACK find_hud_window(HWND window, LPARAM data)
{
    auto *search = reinterpret_cast<HudWindowSearch *>(data);
    if (search == nullptr || search->children == nullptr) {
        return FALSE;
    }

    std::array<wchar_t, 64U> class_name{};
    const int length = GetClassNameW(
        window,
        class_name.data(),
        static_cast<int>(class_name.size()));
    if (length <= 0 ||
        wcscmp(class_name.data(), kHudWindowClassName) != 0) {
        return TRUE;
    }

    DWORD process_id = 0U;
    GetWindowThreadProcessId(window, &process_id);
    if (!contains_process(*search->children, process_id)) {
        return TRUE;
    }

    search->window = window;
    search->process_id = process_id;
    return FALSE;
}

HudWindowSearch current_hud_window() noexcept
{
    const DirectChildren children =
        direct_child_processes(GetCurrentProcessId());
    HudWindowSearch search{&children, nullptr, 0U};
    EnumWindows(&find_hud_window, reinterpret_cast<LPARAM>(&search));
    search.children = nullptr;
    return search;
}

bool create_named_event(
    UniqueHandle &event,
    const std::wstring &name,
    bool manual_reset) noexcept
{
    HANDLE handle = CreateEventW(
        nullptr, manual_reset ? TRUE : FALSE, FALSE, name.c_str());
    const DWORD error = GetLastError();
    event.reset(handle);
    if (!event) {
        log_windows_error("CreateEventW(control center)", error);
        return false;
    }
    if (error == ERROR_ALREADY_EXISTS) {
        blog(LOG_ERROR, "[ChatView OBS] Control Center event name collided");
        event.reset();
        return false;
    }
    return true;
}

} // namespace

ControlCenterBridge::~ControlCenterBridge()
{
    stop();
}

bool ControlCenterBridge::start() noexcept
{
    stop();

    try {
        const std::wstring token = create_transport_token();
        if (!is_valid_transport_token(token)) {
            blog(
                LOG_ERROR,
                "[ChatView OBS] CNG could not create a Control Center token");
            return false;
        }

        const std::wstring suffix =
            std::to_wstring(GetCurrentProcessId()) + L"." + token;
        const std::wstring mapping_name =
            L"Local\\ChatViewOBS.ControlStatus." + suffix;
        const std::wstring status_event_name =
            L"Local\\ChatViewOBS.ControlChanged." + suffix;
        const std::wstring restart_event_name =
            L"Local\\ChatViewOBS.ControlRestart." + suffix;

        UniqueHandle mapping;
        HANDLE mapping_handle = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0U,
            static_cast<DWORD>(sizeof(ControlStatus)),
            mapping_name.c_str());
        const DWORD mapping_error = GetLastError();
        mapping.reset(mapping_handle);
        if (!mapping) {
            log_windows_error(
                "CreateFileMappingW(Control Center)", mapping_error);
            return false;
        }
        if (mapping_error == ERROR_ALREADY_EXISTS) {
            blog(
                LOG_ERROR,
                "[ChatView OBS] Control Center mapping name collided");
            return false;
        }

        ControlStatus *status = static_cast<ControlStatus *>(MapViewOfFile(
            mapping.get(),
            FILE_MAP_ALL_ACCESS,
            0U,
            0U,
            sizeof(ControlStatus)));
        if (status == nullptr) {
            log_windows_error(
                "MapViewOfFile(Control Center)", GetLastError());
            return false;
        }

        UniqueHandle status_event;
        UniqueHandle restart_event;
        if (!create_named_event(
                status_event, status_event_name, false) ||
            !create_named_event(
                restart_event, restart_event_name, false)) {
            UnmapViewOfFile(status);
            return false;
        }

        ZeroMemory(status, sizeof(ControlStatus));
        status->magic = kControlStatusMagic;
        status->version = kControlStatusVersion;

        {
            ExclusiveSrwLockGuard guard(lock_);
            mapping_ = std::move(mapping);
            status_changed_event_ = std::move(status_event);
            restart_event_ = std::move(restart_event);
            status_ = status;
            mapping_name_ = mapping_name;
            status_event_name_ = status_event_name;
            restart_event_name_ = restart_event_name;
            last_flags_ = ControlStatusNone;
            last_hud_process_id_ = 0U;
            last_runtime_telemetry_ = {};
            generation_ = 0U;
            publish_locked(ControlStatusNone, 0U, {});
        }
        return true;
    } catch (const std::exception &error) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] Control Center bridge startup failed: %s",
            error.what());
    } catch (...) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] Control Center bridge startup failed");
    }

    stop();
    return false;
}

void ControlCenterBridge::stop() noexcept
{
    ExclusiveSrwLockGuard guard(lock_);

    if (status_ != nullptr) {
        UnmapViewOfFile(status_);
        status_ = nullptr;
    }

    restart_event_.reset();
    status_changed_event_.reset();
    mapping_.reset();
    mapping_name_.clear();
    status_event_name_.clear();
    restart_event_name_.clear();
    last_flags_ = ControlStatusNone;
    last_hud_process_id_ = 0U;
    last_runtime_telemetry_ = {};
    generation_ = 0U;
}

void ControlCenterBridge::update(
    bool streaming,
    bool recording,
    bool replay_buffer,
    bool virtual_camera,
    bool capture_risk,
    const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept
{
    const HudWindowSearch hud = current_hud_window();

    std::uint32_t flags = ControlStatusNone;
    if (streaming) {
        flags |= ControlStatusStreaming;
    }
    if (recording) {
        flags |= ControlStatusRecording;
    }
    if (replay_buffer) {
        flags |= ControlStatusReplayBuffer;
    }
    if (virtual_camera) {
        flags |= ControlStatusVirtualCamera;
    }
    if (capture_risk) {
        flags |= ControlStatusCaptureRisk;
    }
    if (hud.window != nullptr && hud.process_id != 0U) {
        flags |= ControlStatusHudRunning;
        if (IsWindowVisible(hud.window)) {
            flags |= ControlStatusHudVisible;
        }
    }

    const RuntimeTelemetrySnapshot safe_telemetry =
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
}

bool ControlCenterBridge::open_control_center() noexcept
{
    try {
        std::wstring mapping_name;
        std::wstring status_event_name;
        std::wstring restart_event_name;
        {
            SharedSrwLockGuard guard(lock_);
            if (status_ == nullptr) {
                return false;
            }
            mapping_name = mapping_name_;
            status_event_name = status_event_name_;
            restart_event_name = restart_event_name_;
        }

        const std::wstring path =
            find_sibling_path(kControlCenterExecutableName);
        if (path.empty() || !is_regular_file(path)) {
            blog(
                LOG_ERROR,
                "[ChatView OBS] Control Center was not found: %ls",
                path.c_str());
            return false;
        }

        std::wstring command_line =
            L"\"" + path + L"\" --status-mapping \"" +
            mapping_name + L"\" --status-event \"" +
            status_event_name + L"\" --restart-event \"" +
            restart_event_name + L"\" --parent " +
            std::to_wstring(GetCurrentProcessId());

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
            log_windows_error(
                "CreateProcessW(Control Center)", GetLastError());
            return false;
        }

        UniqueHandle process(process_info.hProcess);
        UniqueHandle thread(process_info.hThread);
        return true;
    } catch (const std::exception &error) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] Failed to open Control Center: %s",
            error.what());
    } catch (...) {
        blog(LOG_ERROR, "[ChatView OBS] Failed to open Control Center");
    }
    return false;
}

bool ControlCenterBridge::consume_restart_request() noexcept
{
    SharedSrwLockGuard guard(lock_);
    if (!restart_event_) {
        return false;
    }

    const DWORD result = WaitForSingleObject(restart_event_.get(), 0U);
    if (result == WAIT_OBJECT_0) {
        return true;
    }
    if (result == WAIT_FAILED) {
        log_windows_error(
            "WaitForSingleObject(Control Center restart)",
            GetLastError());
    }
    return false;
}

std::wstring ControlCenterBridge::find_sibling_path(
    const wchar_t *file_name) const
{
    static int module_anchor = 0;

    HMODULE module_handle = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&module_anchor),
            &module_handle)) {
        log_windows_error("GetModuleHandleExW", GetLastError());
        return {};
    }

    std::array<wchar_t, 32768U> module_path{};
    const DWORD length = GetModuleFileNameW(
        module_handle,
        module_path.data(),
        static_cast<DWORD>(module_path.size()));
    if (length == 0U || length >= module_path.size()) {
        log_windows_error("GetModuleFileNameW", GetLastError());
        return {};
    }

    return (std::filesystem::path(module_path.data()).parent_path() /
            file_name)
        .wstring();
}

void ControlCenterBridge::publish_locked(
    std::uint32_t flags,
    DWORD hud_process_id,
    const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept
{
    if (status_ == nullptr) {
        return;
    }

    InterlockedIncrement(&status_->sequence);
    MemoryBarrier();
    status_->flags = flags;
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
    MemoryBarrier();
    InterlockedIncrement(&status_->sequence);

    last_flags_ = flags;
    last_hud_process_id_ = hud_process_id;
    last_runtime_telemetry_ = runtime_telemetry;
    if (status_changed_event_ &&
        !SetEvent(status_changed_event_.get())) {
        log_windows_error(
            "SetEvent(Control Center status)", GetLastError());
    }
}

} // namespace chatview
