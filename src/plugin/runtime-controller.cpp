// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/runtime-controller.hpp"

#include "common/window-messages.hpp"
#include "plugin/restart-policy.hpp"
#include "plugin/transport-token.hpp"

#include <obs-module.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace chatview {
namespace {

constexpr wchar_t kRuntimeExecutableName[] = L"chat-view-hud.exe";
constexpr DWORD kRuntimeReadyWaitMs = 12000U;
constexpr DWORD kRuntimeExitWaitMs = 2000U;
constexpr DWORD kRuntimeTerminateWaitMs = 2000U;
constexpr DWORD kRuntimeStablePeriodMs = 30000U;

struct RuntimeWindowSearch {
    DWORD process_id = 0U;
    HWND window = nullptr;
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

BOOL CALLBACK find_runtime_window(HWND window, LPARAM data)
{
    auto *search = reinterpret_cast<RuntimeWindowSearch *>(data);
    if (search == nullptr) {
        return FALSE;
    }

    DWORD process_id = 0U;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != search->process_id) {
        return TRUE;
    }

    std::array<wchar_t, 64U> class_name{};
    const int length = GetClassNameW(
        window, class_name.data(), static_cast<int>(class_name.size()));
    if (length > 0 && wcscmp(class_name.data(), kHudWindowClassName) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

} // namespace

RuntimeController::~RuntimeController()
{
    stop();
}

bool RuntimeController::start() noexcept
{
    stop();

    try {
        {
            std::scoped_lock lock(mutex_);
            stopping_.store(false, std::memory_order_release);
            restart_requested_.store(false, std::memory_order_release);

            if (!create_transport_locked()) {
                stopping_.store(true, std::memory_order_release);
                cleanup_locked();
                return false;
            }
        }

        supervisor_thread_ = std::thread(&RuntimeController::supervisor_loop, this);
        blog(LOG_INFO, "[ChatView OBS] HUD supervisor started");
        return true;
    } catch (const std::exception &error) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD supervisor startup failed: %s",
            error.what());
    } catch (...) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD supervisor startup failed with an unknown exception");
    }

    stop();
    return false;
}

void RuntimeController::stop() noexcept
{
    stopping_.store(true, std::memory_order_release);

    {
        ExclusiveSrwLockGuard publish_lock(state_publish_lock_);
        state_publish_handle_ = nullptr;
    }

    if (supervisor_stop_event_ && !SetEvent(supervisor_stop_event_.get())) {
        log_windows_error("SetEvent(supervisor stop)", GetLastError());
    }

    try {
        if (supervisor_thread_.joinable()) {
            supervisor_thread_.join();
        }

        std::scoped_lock lock(mutex_);

        if (shared_state_ != nullptr) {
            publish_locked(
                current_flags_.load(std::memory_order_acquire) |
                SharedStateShutdown);
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
    } catch (const std::exception &error) {
        blog(LOG_ERROR, "[ChatView OBS] HUD shutdown failed: %s", error.what());
        if (runtime_job_) {
            TerminateJobObject(runtime_job_.get(), 1U);
        }
    } catch (...) {
        blog(LOG_ERROR, "[ChatView OBS] HUD shutdown failed with an unknown exception");
        if (runtime_job_) {
            TerminateJobObject(runtime_job_.get(), 1U);
        }
    }
}

void RuntimeController::update(
    bool streaming, bool recording, bool capture_risk) noexcept
{
    std::uint32_t flags = SharedStateNone;
    if (streaming) {
        flags |= SharedStateStreaming;
    }
    if (recording) {
        flags |= SharedStateRecording;
    }
    if (capture_risk) {
        flags |= SharedStateCaptureRisk;
    }

    const std::uint32_t previous =
        current_flags_.exchange(flags, std::memory_order_acq_rel);
    if (previous == flags ||
        stopping_.load(std::memory_order_acquire)) {
        return;
    }

    SharedSrwLockGuard publish_lock(state_publish_lock_);
    if (state_publish_handle_ != nullptr && !SetEvent(state_publish_handle_)) {
        log_windows_error("SetEvent(state publish)", GetLastError());
    }
}


bool RuntimeController::restart_hud() noexcept
{
    if (stopping_.load(std::memory_order_acquire)) {
        return false;
    }

    restart_requested_.store(true, std::memory_order_release);
    SharedSrwLockGuard publish_lock(state_publish_lock_);
    if (state_publish_handle_ == nullptr) {
        restart_requested_.store(false, std::memory_order_release);
        return false;
    }
    if (!SetEvent(state_publish_handle_)) {
        log_windows_error("SetEvent(HUD restart)", GetLastError());
        restart_requested_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

bool RuntimeController::toggle_edit_mode() noexcept
{
    if ((current_flags_.load(std::memory_order_acquire) &
         SharedStateCaptureRisk) != 0U) {
        blog(
            LOG_WARNING,
            "[ChatView OBS] HUD editing is disabled while the "
            "Display Capture interlock is active");
        return false;
    }

    try {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            blog(
                LOG_WARNING,
                "[ChatView OBS] HUD runtime is busy and cannot enter edit mode yet");
            return false;
        }

        if (!runtime_process_ ||
            WaitForSingleObject(runtime_process_.get(), 0U) != WAIT_TIMEOUT) {
            blog(LOG_WARNING, "[ChatView OBS] HUD runtime is not available for editing");
            return false;
        }

        const HWND window = find_runtime_window_locked();
        if (window == nullptr) {
            blog(LOG_WARNING, "[ChatView OBS] HUD window was not found for editing");
            return false;
        }

        const UINT message = RegisterWindowMessageW(kToggleEditMessageName);
        if (message == 0U) {
            log_windows_error("RegisterWindowMessageW(toggle edit)", GetLastError());
            return false;
        }

        if (!PostMessageW(window, message, 0U, 0L)) {
            log_windows_error("PostMessageW(toggle edit)", GetLastError());
            return false;
        }
        return true;
    } catch (const std::exception &error) {
        blog(LOG_ERROR, "[ChatView OBS] Failed to toggle HUD edit mode: %s", error.what());
    } catch (...) {
        blog(LOG_ERROR, "[ChatView OBS] Failed to toggle HUD edit mode");
    }
    return false;
}

bool RuntimeController::create_transport_locked()
{
    const std::wstring token = create_transport_token();
    if (!is_valid_transport_token(token)) {
        blog(LOG_ERROR, "[ChatView OBS] CNG could not create a transport token");
        return false;
    }

    const DWORD process_id = GetCurrentProcessId();
    const std::wstring suffix =
        std::to_wstring(process_id) + L"." + token;
    mapping_name_ = L"Local\\ChatViewOBS.State." + suffix;
    event_name_ = L"Local\\ChatViewOBS.Event." + suffix;
    ready_event_name_ = L"Local\\ChatViewOBS.Ready." + suffix;

    supervisor_stop_event_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!supervisor_stop_event_) {
        log_windows_error("CreateEventW(supervisor stop)", GetLastError());
        return false;
    }

    state_publish_event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!state_publish_event_) {
        log_windows_error("CreateEventW(state publish)", GetLastError());
        return false;
    }

    HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0U,
        static_cast<DWORD>(sizeof(SharedState)),
        mapping_name_.c_str());
    const DWORD mapping_error = GetLastError();
    mapping_.reset(mapping);
    if (!mapping_) {
        log_windows_error("CreateFileMappingW", mapping_error);
        return false;
    }
    if (mapping_error == ERROR_ALREADY_EXISTS) {
        blog(LOG_ERROR, "[ChatView OBS] Transport mapping name collided");
        mapping_.reset();
        return false;
    }

    shared_state_ = static_cast<SharedState *>(MapViewOfFile(
        mapping_.get(),
        FILE_MAP_ALL_ACCESS,
        0U,
        0U,
        sizeof(SharedState)));
    if (shared_state_ == nullptr) {
        log_windows_error("MapViewOfFile", GetLastError());
        return false;
    }

    HANDLE state_changed_event =
        CreateEventW(nullptr, FALSE, FALSE, event_name_.c_str());
    const DWORD state_event_error = GetLastError();
    state_changed_event_.reset(state_changed_event);
    if (!state_changed_event_) {
        log_windows_error("CreateEventW(state changed)", state_event_error);
        return false;
    }
    if (state_event_error == ERROR_ALREADY_EXISTS) {
        blog(LOG_ERROR, "[ChatView OBS] State event name collided");
        state_changed_event_.reset();
        return false;
    }

    HANDLE runtime_ready_event =
        CreateEventW(nullptr, TRUE, FALSE, ready_event_name_.c_str());
    const DWORD ready_event_error = GetLastError();
    runtime_ready_event_.reset(runtime_ready_event);
    if (!runtime_ready_event_) {
        log_windows_error("CreateEventW(runtime ready)", ready_event_error);
        return false;
    }
    if (ready_event_error == ERROR_ALREADY_EXISTS) {
        blog(LOG_ERROR, "[ChatView OBS] Ready event name collided");
        runtime_ready_event_.reset();
        return false;
    }

    ZeroMemory(shared_state_, sizeof(SharedState));
    shared_state_->magic = kSharedStateMagic;
    shared_state_->version = kSharedStateVersion;
    shared_state_->sequence = 0;
    shared_state_->flags = SharedStateNone;
    shared_state_->generation = 0U;

    {
        ExclusiveSrwLockGuard publish_lock(state_publish_lock_);
        state_publish_handle_ = state_publish_event_.get();
    }
    return true;
}

bool RuntimeController::create_runtime_job_locked()
{
    runtime_job_.reset();
    runtime_job_.reset(CreateJobObjectW(nullptr, nullptr));
    if (!runtime_job_) {
        log_windows_error("CreateJobObjectW", GetLastError());
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(
            runtime_job_.get(),
            JobObjectExtendedLimitInformation,
            &limits,
            static_cast<DWORD>(sizeof(limits)))) {
        log_windows_error("SetInformationJobObject", GetLastError());
        runtime_job_.reset();
        return false;
    }
    return true;
}

bool RuntimeController::launch_runtime_locked()
{
    const std::wstring runtime_path =
        find_sibling_path(kRuntimeExecutableName);
    if (runtime_path.empty() || !is_regular_file(runtime_path)) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD runtime was not found: %ls",
            runtime_path.c_str());
        return false;
    }

    if (!ResetEvent(runtime_ready_event_.get())) {
        log_windows_error("ResetEvent(runtime ready)", GetLastError());
        return false;
    }

    if (!create_runtime_job_locked()) {
        return false;
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
        return false;
    }

    UniqueHandle thread(process_info.hThread);
    UniqueHandle process(process_info.hProcess);

    if (!AssignProcessToJobObject(runtime_job_.get(), process.get())) {
        log_windows_error("AssignProcessToJobObject(HUD)", GetLastError());
        TerminateProcess(process.get(), 1U);
        WaitForSingleObject(process.get(), kRuntimeTerminateWaitMs);
        runtime_job_.reset();
        return false;
    }

    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        log_windows_error("ResumeThread(HUD)", GetLastError());
        TerminateProcess(process.get(), 1U);
        WaitForSingleObject(process.get(), kRuntimeTerminateWaitMs);
        runtime_job_.reset();
        return false;
    }

    HANDLE wait_handles[3] = {
        runtime_ready_event_.get(),
        process.get(),
        supervisor_stop_event_.get(),
    };
    const DWORD wait_result =
        WaitForMultipleObjects(3U, wait_handles, FALSE, kRuntimeReadyWaitMs);
    if (wait_result == WAIT_OBJECT_0 + 1U) {
        log_process_exit(process.get(), "exited before reporting ready");
        runtime_job_.reset();
        return false;
    }
    if (wait_result == WAIT_OBJECT_0 + 2U) {
        TerminateProcess(process.get(), 0U);
        WaitForSingleObject(process.get(), kRuntimeTerminateWaitMs);
        runtime_job_.reset();
        return false;
    }
    if (wait_result != WAIT_OBJECT_0) {
        if (wait_result == WAIT_FAILED) {
            log_windows_error(
                "WaitForMultipleObjects(HUD ready)", GetLastError());
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
        runtime_job_.reset();
        return false;
    }

    runtime_process_id_ = process_info.dwProcessId;
    runtime_process_ = std::move(process);
    return true;
}

HWND RuntimeController::find_runtime_window_locked() const noexcept
{
    if (runtime_process_id_ == 0U) {
        return nullptr;
    }

    RuntimeWindowSearch search{runtime_process_id_, nullptr};
    EnumWindows(&find_runtime_window, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

std::wstring RuntimeController::find_sibling_path(
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

void RuntimeController::supervisor_loop() noexcept
{
    RestartPolicy restart_policy;
    ULONGLONG retry_deadline = 0U;

    const auto record_failure = [&restart_policy, &retry_deadline]() noexcept {
        restart_policy.record_failure();
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

                    restart_policy.reset();
                    retry_deadline = 0U;
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

                bool launched = false;
                {
                    std::scoped_lock lock(mutex_);
                    if (stopping_.load(std::memory_order_acquire)) {
                        return;
                    }
                    launched = launch_runtime_locked();
                    if (launched) {
                        publish_locked(
                            current_flags_.load(
                                std::memory_order_acquire));
                    }
                }

                if (stopping_.load(std::memory_order_acquire)) {
                    return;
                }
                if (launched) {
                    retry_deadline = 0U;
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] HUD runtime started and reported "
                        "ready");
                } else {
                    record_failure();
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
                    restart_policy.reset();
                    retry_deadline = 0U;
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] Manual HUD restart accepted");
                } else if (
                    WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
                    publish_locked(
                        current_flags_.load(
                            std::memory_order_acquire));
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
                restart_policy.reset();
                retry_deadline = 0U;
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
            {
                std::scoped_lock lock(mutex_);
                if (runtime_process_id_ == process_id &&
                    runtime_process_.get() == process) {
                    log_process_exit(
                        runtime_process_.get(), "exited unexpectedly");
                    runtime_process_.reset();
                    runtime_process_id_ = 0U;
                    runtime_job_.reset();
                    exited = true;
                }
            }
            if (exited) {
                record_failure();
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

    if (state_changed_event_ && !SetEvent(state_changed_event_.get())) {
        log_windows_error("SetEvent(state changed)", GetLastError());
    }
}

void RuntimeController::terminate_runtime_locked(
    const char *reason) noexcept
{
    if (!runtime_process_) {
        runtime_job_.reset();
        return;
    }

    blog(
        LOG_WARNING,
        "[ChatView OBS] HUD runtime %s; terminating it",
        reason);
    if (!TerminateProcess(runtime_process_.get(), 1U)) {
        log_windows_error("TerminateProcess(HUD)", GetLastError());
        if (runtime_job_ && !TerminateJobObject(runtime_job_.get(), 1U)) {
            log_windows_error("TerminateJobObject(HUD)", GetLastError());
        }
    }

    const DWORD wait_result =
        WaitForSingleObject(runtime_process_.get(), kRuntimeTerminateWaitMs);
    if (wait_result == WAIT_TIMEOUT && runtime_job_) {
        if (!TerminateJobObject(runtime_job_.get(), 1U)) {
            log_windows_error(
                "TerminateJobObject(HUD timeout)", GetLastError());
        }
        WaitForSingleObject(
            runtime_process_.get(), kRuntimeTerminateWaitMs);
    } else if (wait_result == WAIT_FAILED) {
        log_windows_error(
            "WaitForSingleObject(HUD terminate)", GetLastError());
    }

    runtime_process_.reset();
    runtime_process_id_ = 0U;
    runtime_job_.reset();
}

void RuntimeController::cleanup_locked() noexcept
{
    {
        ExclusiveSrwLockGuard publish_lock(state_publish_lock_);
        state_publish_handle_ = nullptr;
        state_publish_event_.reset();
    }

    if (shared_state_ != nullptr) {
        UnmapViewOfFile(shared_state_);
        shared_state_ = nullptr;
    }

    runtime_process_.reset();
    runtime_process_id_ = 0U;
    runtime_ready_event_.reset();
    state_changed_event_.reset();
    mapping_.reset();
    runtime_job_.reset();
    supervisor_stop_event_.reset();
    mapping_name_.clear();
    event_name_.clear();
    ready_event_name_.clear();
    generation_ = 0U;
    restart_requested_.store(false, std::memory_order_release);
    current_flags_.store(SharedStateNone, std::memory_order_release);
}

} // namespace chatview
