// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/shared-state.hpp"
#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"

#include <Windows.h>
#include <Psapi.h>
#include <WtsApi32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kHudWindowClass[] = L"ChatViewObsHudWindow";
constexpr DWORD kStartupTimeoutMs = 25000U;
constexpr DWORD kTransitionTimeoutMs = 7000U;
constexpr DWORD kShutdownTimeoutMs = 8000U;
constexpr DWORD kJobDrainTimeoutMs = 5000U;
constexpr unsigned int kExerciseCycles = 24U;
constexpr std::uint64_t kMaximumPrivateGrowth = 128ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumHandleGrowth = 128ULL;
constexpr std::uint64_t kMaximumGuiGrowth = 32ULL;
constexpr std::uint64_t kMaximumProcessGrowth = 3ULL;

struct WindowSearch {
    DWORD process_id = 0U;
    HWND window = nullptr;
};

struct ResourceSample {
    std::uint64_t process_count = 0U;
    std::uint64_t handle_count = 0U;
    std::uint64_t private_bytes = 0U;
    std::uint64_t gdi_objects = 0U;
    std::uint64_t user_objects = 0U;
};

class MappedState final {
public:
    explicit MappedState(chatview::SharedState *state) noexcept : state_(state) {}

    ~MappedState()
    {
        if (state_ != nullptr) {
            UnmapViewOfFile(state_);
        }
    }

    MappedState(const MappedState &) = delete;
    MappedState &operator=(const MappedState &) = delete;

    [[nodiscard]] chatview::SharedState *get() const noexcept
    {
        return state_;
    }

private:
    chatview::SharedState *state_ = nullptr;
};

int fail(const wchar_t *message, HANDLE process = nullptr)
{
    std::wcerr << message << L'\n';
    if (process != nullptr &&
        WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
        TerminateProcess(process, 1U);
        WaitForSingleObject(process, 2000U);
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
    const int length = GetClassNameW(
        window, class_name.data(), static_cast<int>(class_name.size()));
    if (length > 0 && wcscmp(class_name.data(), kHudWindowClass) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HWND wait_for_hud_window(HANDLE process, DWORD process_id) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kTransitionTimeoutMs;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(process, 0U) != WAIT_TIMEOUT) {
            return nullptr;
        }

        WindowSearch search{process_id, nullptr};
        EnumWindows(&find_hud_window, reinterpret_cast<LPARAM>(&search));
        if (search.window != nullptr) {
            return search.window;
        }
        Sleep(25U);
    }
    return nullptr;
}

bool wait_for_visibility(HWND window, bool visible) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kTransitionTimeoutMs;
    while (GetTickCount64() < deadline) {
        if ((IsWindowVisible(window) != FALSE) == visible) {
            return true;
        }
        Sleep(25U);
    }
    return false;
}

bool wait_for_style(
    HWND window, LONG_PTR required, LONG_PTR forbidden) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kTransitionTimeoutMs;
    while (GetTickCount64() < deadline) {
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if ((style & required) == required && (style & forbidden) == 0) {
            return true;
        }
        Sleep(25U);
    }
    return false;
}

void publish(
    chatview::SharedState *state,
    HANDLE event,
    std::uint32_t flags) noexcept
{
    InterlockedIncrement(&state->sequence);
    MemoryBarrier();
    state->flags = flags;
    ++state->generation;
    MemoryBarrier();
    InterlockedIncrement(&state->sequence);
    SetEvent(event);
}

bool configure_child_job(HANDLE job) noexcept
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return SetInformationJobObject(
               job,
               JobObjectExtendedLimitInformation,
               &limits,
               static_cast<DWORD>(sizeof(limits))) != FALSE;
}

bool wait_for_job_empty(HANDLE job, DWORD timeout_ms) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (!QueryInformationJobObject(
                job,
                JobObjectBasicAccountingInformation,
                &accounting,
                static_cast<DWORD>(sizeof(accounting)),
                nullptr)) {
            return false;
        }
        if (accounting.ActiveProcesses == 0U) {
            return true;
        }
        Sleep(50U);
    } while (GetTickCount64() < deadline);
    return false;
}

bool query_job_process_ids(
    HANDLE job, std::vector<DWORD> &process_ids) noexcept
{
    std::vector<std::byte> buffer(4096U);
    for (unsigned int attempt = 0U; attempt < 4U; ++attempt) {
        DWORD returned = 0U;
        if (QueryInformationJobObject(
                job,
                JobObjectBasicProcessIdList,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &returned)) {
            const auto *list = reinterpret_cast<
                const JOBOBJECT_BASIC_PROCESS_ID_LIST *>(buffer.data());
            process_ids.clear();
            process_ids.reserve(list->NumberOfProcessIdsInList);
            for (DWORD index = 0U;
                 index < list->NumberOfProcessIdsInList;
                 ++index) {
                process_ids.push_back(
                    static_cast<DWORD>(list->ProcessIdList[index]));
            }
            return true;
        }

        if (GetLastError() != ERROR_MORE_DATA) {
            return false;
        }
        buffer.resize(buffer.size() * 2U);
    }
    return false;
}

bool sample_job_once(HANDLE job, ResourceSample &sample) noexcept
{
    std::vector<DWORD> process_ids;
    if (!query_job_process_ids(job, process_ids) || process_ids.empty()) {
        return false;
    }

    ResourceSample candidate;
    for (const DWORD process_id : process_ids) {
        chatview::UniqueHandle process(OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
            FALSE,
            process_id));
        if (!process) {
            return false;
        }

        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (!GetProcessMemoryInfo(
                process.get(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                sizeof(memory))) {
            return false;
        }

        DWORD handles = 0U;
        if (!GetProcessHandleCount(process.get(), &handles)) {
            return false;
        }

        ++candidate.process_count;
        candidate.handle_count += handles;
        candidate.private_bytes += memory.PrivateUsage;
        candidate.gdi_objects +=
            GetGuiResources(process.get(), GR_GDIOBJECTS);
        candidate.user_objects +=
            GetGuiResources(process.get(), GR_USEROBJECTS);
    }

    sample = candidate;
    return true;
}

bool sample_job(HANDLE job, ResourceSample &sample) noexcept
{
    for (unsigned int attempt = 0U; attempt < 20U; ++attempt) {
        if (sample_job_once(job, sample)) {
            return true;
        }
        Sleep(50U);
    }
    return false;
}

void update_maximum(
    ResourceSample &maximum, const ResourceSample &sample) noexcept
{
    maximum.process_count =
        std::max(maximum.process_count, sample.process_count);
    maximum.handle_count =
        std::max(maximum.handle_count, sample.handle_count);
    maximum.private_bytes =
        std::max(maximum.private_bytes, sample.private_bytes);
    maximum.gdi_objects =
        std::max(maximum.gdi_objects, sample.gdi_objects);
    maximum.user_objects =
        std::max(maximum.user_objects, sample.user_objects);
}

bool remove_tree_with_retry(const std::filesystem::path &path) noexcept
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::error_code error;
    do {
        error.clear();
        std::filesystem::remove_all(path, error);
        if (!error && !std::filesystem::exists(path, error)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool exercise_capture_cycle(
    HWND window,
    chatview::SharedState *state,
    HANDLE event) noexcept
{
    publish(
        state,
        event,
        chatview::SharedStateStreaming |
            chatview::SharedStateCaptureRisk);
    if (!wait_for_visibility(window, false)) {
        return false;
    }

    publish(state, event, chatview::SharedStateStreaming);
    return wait_for_visibility(window, true);
}

bool exercise_edit_cycle(HWND window, UINT toggle_message) noexcept
{
    constexpr LONG_PTR locked_style =
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
        WS_EX_NOREDIRECTIONBITMAP;
    constexpr LONG_PTR edit_style =
        WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP;

    if (!PostMessageW(window, toggle_message, 0U, 0L) ||
        !wait_for_style(
            window,
            edit_style,
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) {
        return false;
    }
    if (!PostMessageW(window, toggle_message, 0U, 0L)) {
        return false;
    }
    return wait_for_style(window, locked_style, 0);
}

bool exercise_lifecycle_cycle(HWND window) noexcept
{
    SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0L);
    if (!wait_for_visibility(window, false)) {
        return false;
    }

    if (SendMessageW(
            window, WM_POWERBROADCAST, PBT_APMSUSPEND, 0L) != TRUE ||
        SendMessageW(
            window,
            WM_POWERBROADCAST,
            PBT_APMRESUMEAUTOMATIC,
            0L) != TRUE) {
        return false;
    }
    Sleep(250U);
    if (IsWindowVisible(window)) {
        return false;
    }

    SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0L);
    return wait_for_visibility(window, true);
}

} // namespace

int wmain(int argument_count, wchar_t **arguments)
{
    if (argument_count != 2) {
        return fail(L"Expected the HUD executable path");
    }

    const std::filesystem::path hud_path = arguments[1];
    if (!std::filesystem::is_regular_file(hud_path)) {
        return fail(L"The HUD executable does not exist");
    }

    const DWORD parent_process_id = GetCurrentProcessId();
    const std::wstring suffix = std::to_wstring(parent_process_id);
    const std::wstring mapping_name =
        L"Local\\ChatViewOBS.Soak.State." + suffix;
    const std::wstring event_name =
        L"Local\\ChatViewOBS.Soak.Event." + suffix;
    const std::wstring ready_event_name =
        L"Local\\ChatViewOBS.Soak.Ready." + suffix;

    chatview::UniqueHandle mapping(CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0U,
        static_cast<DWORD>(sizeof(chatview::SharedState)),
        mapping_name.c_str()));
    if (!mapping) {
        return fail(L"Failed to create the soak-test mapping");
    }

    MappedState mapped_state(static_cast<chatview::SharedState *>(
        MapViewOfFile(
            mapping.get(),
            FILE_MAP_ALL_ACCESS,
            0U,
            0U,
            sizeof(chatview::SharedState))));
    if (mapped_state.get() == nullptr) {
        return fail(L"Failed to map the soak-test state");
    }

    chatview::UniqueHandle state_event(
        CreateEventW(nullptr, FALSE, FALSE, event_name.c_str()));
    chatview::UniqueHandle ready_event(
        CreateEventW(nullptr, TRUE, FALSE, ready_event_name.c_str()));
    chatview::UniqueHandle child_job(CreateJobObjectW(nullptr, nullptr));
    if (!state_event || !ready_event || !child_job ||
        !configure_child_job(child_job.get())) {
        return fail(L"Failed to create soak-test synchronization objects");
    }

    ZeroMemory(mapped_state.get(), sizeof(chatview::SharedState));
    mapped_state.get()->magic = chatview::kSharedStateMagic;
    mapped_state.get()->version = chatview::kSharedStateVersion;
    mapped_state.get()->flags = chatview::SharedStateStreaming;
    mapped_state.get()->generation = 1U;

    const std::filesystem::path local_app_data =
        std::filesystem::temp_directory_path() /
        (L"chatview-hud-resource-soak-" + suffix);
    if (!remove_tree_with_retry(local_app_data)) {
        return fail(L"Failed to reset the soak-test profile directory");
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(local_app_data, filesystem_error);
    if (filesystem_error ||
        !SetEnvironmentVariableW(
            L"LOCALAPPDATA", local_app_data.c_str())) {
        return fail(L"Failed to prepare the soak-test profile directory");
    }

    std::wstring command_line =
        L"\"" + hud_path.wstring() + L"\" --mapping \"" + mapping_name +
        L"\" --event \"" + event_name + L"\" --ready-event \"" +
        ready_event_name + L"\" --parent " + suffix;

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION child_info{};
    if (!CreateProcessW(
            hud_path.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
            nullptr,
            nullptr,
            &startup_info,
            &child_info)) {
        return fail(L"Failed to start the HUD for resource soak testing");
    }

    chatview::UniqueHandle child_thread(child_info.hThread);
    chatview::UniqueHandle child_process(child_info.hProcess);
    if (!AssignProcessToJobObject(child_job.get(), child_process.get()) ||
        ResumeThread(child_thread.get()) == static_cast<DWORD>(-1)) {
        TerminateJobObject(child_job.get(), 1U);
        return fail(L"Failed to start the HUD process tree");
    }
    child_thread.reset();

    HANDLE startup_handles[2] = {ready_event.get(), child_process.get()};
    const DWORD startup_result = WaitForMultipleObjects(
        2U, startup_handles, FALSE, kStartupTimeoutMs);
    if (startup_result != WAIT_OBJECT_0) {
        return fail(
            L"The HUD did not report readiness during resource soak testing",
            child_process.get());
    }

    HWND window = wait_for_hud_window(
        child_process.get(), child_info.dwProcessId);
    if (window == nullptr || !wait_for_visibility(window, true)) {
        return fail(
            L"The ready HUD window was not visible for resource soak testing",
            child_process.get());
    }

    const UINT toggle_message = RegisterWindowMessageW(
        chatview::kToggleEditMessageName);
    const UINT config_message = RegisterWindowMessageW(
        chatview::kConfigChangedMessageName);
    if (toggle_message == 0U || config_message == 0U) {
        return fail(
            L"Failed to register HUD control messages",
            child_process.get());
    }

    for (unsigned int warmup = 0U; warmup < 3U; ++warmup) {
        if (!exercise_capture_cycle(
                window, mapped_state.get(), state_event.get()) ||
            !exercise_edit_cycle(window, toggle_message)) {
            return fail(
                L"HUD warm-up failed before resource sampling",
                child_process.get());
        }
    }
    Sleep(1500U);

    ResourceSample baseline;
    ResourceSample maximum;
    if (!sample_job(child_job.get(), baseline)) {
        return fail(
            L"Failed to sample the HUD process tree baseline",
            child_process.get());
    }
    maximum = baseline;

    for (unsigned int cycle = 0U; cycle < kExerciseCycles; ++cycle) {
        if (!exercise_capture_cycle(
                window, mapped_state.get(), state_event.get()) ||
            !exercise_edit_cycle(window, toggle_message)) {
            return fail(
                L"The HUD failed during repeated resource exercise",
                child_process.get());
        }

        if ((cycle % 4U) == 0U) {
            PostMessageW(window, config_message, 0U, 0L);
        }
        if (cycle == kExerciseCycles / 2U &&
            !exercise_lifecycle_cycle(window)) {
            return fail(
                L"The HUD failed during the soak lifecycle transition",
                child_process.get());
        }

        if ((cycle % 3U) == 0U) {
            ResourceSample sample;
            if (!sample_job(child_job.get(), sample)) {
                return fail(
                    L"Failed to sample the live HUD process tree",
                    child_process.get());
            }
            update_maximum(maximum, sample);
        }
    }

    Sleep(2500U);
    ResourceSample final_sample;
    if (!sample_job(child_job.get(), final_sample)) {
        return fail(
            L"Failed to sample the final HUD process tree",
            child_process.get());
    }
    update_maximum(maximum, final_sample);

    std::wcout
        << L"HUD resource soak baseline: processes=" << baseline.process_count
        << L" handles=" << baseline.handle_count
        << L" private_bytes=" << baseline.private_bytes
        << L" gdi=" << baseline.gdi_objects
        << L" user=" << baseline.user_objects << L'\n'
        << L"HUD resource soak final: processes=" << final_sample.process_count
        << L" handles=" << final_sample.handle_count
        << L" private_bytes=" << final_sample.private_bytes
        << L" gdi=" << final_sample.gdi_objects
        << L" user=" << final_sample.user_objects << L'\n'
        << L"HUD resource soak peak: processes=" << maximum.process_count
        << L" handles=" << maximum.handle_count
        << L" private_bytes=" << maximum.private_bytes
        << L" gdi=" << maximum.gdi_objects
        << L" user=" << maximum.user_objects << L'\n';

    if (final_sample.process_count >
            baseline.process_count + kMaximumProcessGrowth ||
        final_sample.handle_count >
            baseline.handle_count + kMaximumHandleGrowth ||
        final_sample.private_bytes >
            baseline.private_bytes + kMaximumPrivateGrowth ||
        final_sample.gdi_objects >
            baseline.gdi_objects + kMaximumGuiGrowth ||
        final_sample.user_objects >
            baseline.user_objects + kMaximumGuiGrowth) {
        return fail(
            L"The HUD process tree exceeded the resource-growth budget",
            child_process.get());
    }

    DWORD affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != 0x00000011U) {
        return fail(
            L"The HUD lost capture exclusion during resource soak testing",
            child_process.get());
    }

    publish(
        mapped_state.get(),
        state_event.get(),
        chatview::SharedStateStreaming | chatview::SharedStateShutdown);
    if (WaitForSingleObject(
            child_process.get(), kShutdownTimeoutMs) != WAIT_OBJECT_0) {
        return fail(
            L"The HUD did not exit after resource soak testing",
            child_process.get());
    }

    DWORD exit_code = 1U;
    if (!GetExitCodeProcess(child_process.get(), &exit_code) ||
        exit_code != 0U) {
        return fail(L"The HUD exited with an error after resource soak testing");
    }
    if (!wait_for_job_empty(child_job.get(), kJobDrainTimeoutMs)) {
        TerminateJobObject(child_job.get(), 1U);
        if (!wait_for_job_empty(child_job.get(), kJobDrainTimeoutMs)) {
            return fail(L"The HUD process tree did not drain after soak testing");
        }
    }

    child_process.reset();
    child_job.reset();
    if (!remove_tree_with_retry(local_app_data)) {
        return fail(L"Failed to remove the soak-test profile directory");
    }
    return 0;
}
