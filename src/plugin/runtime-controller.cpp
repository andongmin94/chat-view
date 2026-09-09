// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/runtime-controller.hpp"

#include "common/window-messages.hpp"

#include <obs-module.h>

#include <Windows.h>

#include <array>
#include <exception>
#include <filesystem>
#include <string>

namespace chatview {
namespace {

constexpr wchar_t kRuntimeExecutableName[] = L"chat-view-hud.exe";
constexpr wchar_t kSettingsExecutableName[] = L"chat-view-config.exe";
constexpr DWORD kRuntimeReadyWaitMs = 12000U;
constexpr DWORD kRuntimeExitWaitMs = 2000U;
constexpr DWORD kRuntimeTerminateWaitMs = 2000U;

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

void log_process_exit(HANDLE process, const char *context) noexcept
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

} // namespace

RuntimeController::~RuntimeController()
{
    stop();
}

bool RuntimeController::start() noexcept
{
    try {
        std::scoped_lock lock(mutex_);
        cleanup_locked();

        if (!create_transport_locked() || !launch_runtime_locked(true)) {
            cleanup_locked();
            return false;
        }

        publish_locked(current_flags_);
        blog(LOG_INFO, "[ChatView OBS] HUD runtime started and reported ready");
        return true;
    } catch (const std::exception &error) {
        blog(LOG_ERROR, "[ChatView OBS] HUD startup failed: %s", error.what());
    } catch (...) {
        blog(LOG_ERROR, "[ChatView OBS] HUD startup failed with an unknown exception");
    }

    stop();
    return false;
}

void RuntimeController::stop() noexcept
{
    std::scoped_lock lock(mutex_);

    if (shared_state_ != nullptr) {
        publish_locked(current_flags_ | SharedStateShutdown);
    }

    if (runtime_process_) {
        const DWORD wait_result =
            WaitForSingleObject(runtime_process_.get(), kRuntimeExitWaitMs);
        if (wait_result == WAIT_TIMEOUT) {
            terminate_runtime_locked("did not exit after the shutdown request");
        } else if (wait_result == WAIT_FAILED) {
            log_windows_error("WaitForSingleObject(HUD shutdown)", GetLastError());
            terminate_runtime_locked("could not be observed during shutdown");
        }
    }

    cleanup_locked();
}

void RuntimeController::update(bool streaming, bool recording) noexcept
{
    try {
        std::scoped_lock lock(mutex_);

        std::uint32_t flags = SharedStateNone;
        if (streaming) {
            flags |= SharedStateStreaming;
        }
        if (recording) {
            flags |= SharedStateRecording;
        }
        current_flags_ = flags;

        if (shared_state_ == nullptr || !ensure_runtime_locked()) {
            return;
        }
        publish_locked(current_flags_);
    } catch (const std::exception &error) {
        blog(LOG_ERROR, "[ChatView OBS] Failed to publish HUD state: %s", error.what());
    } catch (...) {
        blog(LOG_ERROR, "[ChatView OBS] Failed to publish HUD state");
    }
}

bool RuntimeController::open_settings() const noexcept
{
    try {
        const std::wstring path = find_sibling_path(kSettingsExecutableName);
        if (path.empty() || !is_regular_file(path)) {
            blog(LOG_ERROR, "[ChatView OBS] Settings application was not found: %ls", path.c_str());
            return false;
        }

        std::wstring command_line = L"\"" + path + L"\"";
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

bool RuntimeController::toggle_edit_mode() const noexcept
{
    const UINT message = RegisterWindowMessageW(kToggleEditMessageName);
    if (message == 0U) {
        log_windows_error("RegisterWindowMessageW(toggle edit)", GetLastError());
        return false;
    }

    if (!SendNotifyMessageW(HWND_BROADCAST, message, 0U, 0L)) {
        log_windows_error("SendNotifyMessageW(toggle edit)", GetLastError());
        return false;
    }
    return true;
}

bool RuntimeController::create_transport_locked()
{
    const DWORD process_id = GetCurrentProcessId();
    const std::wstring suffix = std::to_wstring(process_id);
    mapping_name_ = L"Local\\ChatViewOBS.State." + suffix;
    event_name_ = L"Local\\ChatViewOBS.Event." + suffix;
    ready_event_name_ = L"Local\\ChatViewOBS.Ready." + suffix;

    mapping_.reset(CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0U,
        static_cast<DWORD>(sizeof(SharedState)),
        mapping_name_.c_str()));
    if (!mapping_) {
        log_windows_error("CreateFileMappingW", GetLastError());
        return false;
    }

    shared_state_ = static_cast<SharedState *>(
        MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0U, 0U, sizeof(SharedState)));
    if (shared_state_ == nullptr) {
        log_windows_error("MapViewOfFile", GetLastError());
        return false;
    }

    state_changed_event_.reset(CreateEventW(nullptr, FALSE, FALSE, event_name_.c_str()));
    if (!state_changed_event_) {
        log_windows_error("CreateEventW(state changed)", GetLastError());
        return false;
    }

    runtime_ready_event_.reset(CreateEventW(nullptr, TRUE, FALSE, ready_event_name_.c_str()));
    if (!runtime_ready_event_) {
        log_windows_error("CreateEventW(runtime ready)", GetLastError());
        return false;
    }

    ZeroMemory(shared_state_, sizeof(SharedState));
    shared_state_->magic = kSharedStateMagic;
    shared_state_->version = kSharedStateVersion;
    shared_state_->sequence = 0;
    shared_state_->flags = SharedStateNone;
    shared_state_->generation = 0U;
    return true;
}

bool RuntimeController::ensure_runtime_locked()
{
    if (runtime_process_ &&
        WaitForSingleObject(runtime_process_.get(), 0U) == WAIT_TIMEOUT) {
        return true;
    }

    if (runtime_process_) {
        log_process_exit(runtime_process_.get(), "exited unexpectedly");
        runtime_process_.reset();
    }
    return launch_runtime_locked(false);
}

bool RuntimeController::launch_runtime_locked(bool wait_until_ready)
{
    const std::wstring runtime_path = find_sibling_path(kRuntimeExecutableName);
    if (runtime_path.empty() || !is_regular_file(runtime_path)) {
        blog(LOG_ERROR, "[ChatView OBS] HUD runtime was not found: %ls", runtime_path.c_str());
        return false;
    }

    if (!ResetEvent(runtime_ready_event_.get())) {
        log_windows_error("ResetEvent(runtime ready)", GetLastError());
        return false;
    }

    const DWORD process_id = GetCurrentProcessId();
    std::wstring command_line =
        L"\"" + runtime_path + L"\" --mapping \"" + mapping_name_ +
        L"\" --event \"" + event_name_ + L"\" --ready-event \"" +
        ready_event_name_ + L"\" --parent " + std::to_wstring(process_id);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(
            runtime_path.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            nullptr,
            &startup_info,
            &process_info)) {
        log_windows_error("CreateProcessW(HUD)", GetLastError());
        return false;
    }

    UniqueHandle thread(process_info.hThread);
    UniqueHandle process(process_info.hProcess);

    if (wait_until_ready) {
        HANDLE wait_handles[2] = {runtime_ready_event_.get(), process.get()};
        const DWORD wait_result =
            WaitForMultipleObjects(2U, wait_handles, FALSE, kRuntimeReadyWaitMs);
        if (wait_result == WAIT_OBJECT_0 + 1U) {
            log_process_exit(process.get(), "exited before reporting ready");
            return false;
        }
        if (wait_result != WAIT_OBJECT_0) {
            if (wait_result == WAIT_FAILED) {
                log_windows_error("WaitForMultipleObjects(HUD ready)", GetLastError());
            } else {
                blog(
                    LOG_ERROR,
                    "[ChatView OBS] HUD did not report ready within %lu ms",
                    static_cast<unsigned long>(kRuntimeReadyWaitMs));
            }

            if (WaitForSingleObject(process.get(), 0U) == WAIT_TIMEOUT) {
                TerminateProcess(process.get(), 1U);
                WaitForSingleObject(process.get(), kRuntimeTerminateWaitMs);
            }
            return false;
        }
    }

    runtime_process_ = std::move(process);
    return true;
}

std::wstring RuntimeController::find_sibling_path(const wchar_t *file_name) const
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
        module_handle, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length == 0U || length >= module_path.size()) {
        log_windows_error("GetModuleFileNameW", GetLastError());
        return {};
    }

    return (std::filesystem::path(module_path.data()).parent_path() / file_name).wstring();
}

void RuntimeController::publish_locked(std::uint32_t flags) noexcept
{
    if (shared_state_ == nullptr) {
        return;
    }

    InterlockedIncrement(&shared_state_->sequence);
    MemoryBarrier();
    shared_state_->flags = flags;
    shared_state_->generation = ++generation_;
    MemoryBarrier();
    InterlockedIncrement(&shared_state_->sequence);

    if (state_changed_event_) {
        SetEvent(state_changed_event_.get());
    }
}

void RuntimeController::terminate_runtime_locked(const char *reason) noexcept
{
    if (!runtime_process_) {
        return;
    }

    blog(LOG_WARNING, "[ChatView OBS] HUD runtime %s; terminating it", reason);
    if (!TerminateProcess(runtime_process_.get(), 1U)) {
        log_windows_error("TerminateProcess(HUD)", GetLastError());
        return;
    }
    WaitForSingleObject(runtime_process_.get(), kRuntimeTerminateWaitMs);
}

void RuntimeController::cleanup_locked() noexcept
{
    if (shared_state_ != nullptr) {
        UnmapViewOfFile(shared_state_);
        shared_state_ = nullptr;
    }

    runtime_process_.reset();
    runtime_ready_event_.reset();
    state_changed_event_.reset();
    mapping_.reset();
    mapping_name_.clear();
    event_name_.clear();
    ready_event_name_.clear();
    generation_ = 0U;
    current_flags_ = SharedStateNone;
}

} // namespace chatview
