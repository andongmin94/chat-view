// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/capture-risk-policy.hpp"
#include "plugin/control-center-bridge.hpp"
#include "plugin/runtime-controller.hpp"
#include "common/win32-handle.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <Windows.h>

#include <array>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("chat-view-obs", "en-US")

namespace {

constexpr UINT kCaptureRiskPollIntervalMs = 100U;
constexpr ULONGLONG kOutputStartPendingTimeoutMs = 15000U;
constexpr ULONGLONG kPostStartConservativeScanMs = 1000U;
constexpr wchar_t kDiagnosticsExecutableName[] = L"chat-view-diagnostics.exe";

struct CaptureScan {
    bool display_capture_present = false;
    bool display_capture_active_or_showing = false;
};

std::unique_ptr<chatview::RuntimeController> runtime_controller;
std::unique_ptr<chatview::ControlCenterBridge> control_center_bridge;
UINT_PTR capture_risk_timer_id = 0U;
bool frontend_callback_registered = false;
bool scene_graph_stable = false;
ULONGLONG streaming_start_deadline = 0U;
ULONGLONG recording_start_deadline = 0U;
ULONGLONG replay_buffer_start_deadline = 0U;
ULONGLONG conservative_scan_deadline = 0U;
bool published_capture_state = false;
bool last_capture_suppressed = false;

bool deadline_active(
    ULONGLONG deadline, ULONGLONG now) noexcept
{
    return deadline != 0U && now < deadline;
}

void begin_output_start(ULONGLONG &deadline) noexcept
{
    deadline = GetTickCount64() + kOutputStartPendingTimeoutMs;
}

void complete_output_start(ULONGLONG &deadline) noexcept
{
    deadline = 0U;
    const ULONGLONG candidate =
        GetTickCount64() + kPostStartConservativeScanMs;
    if (candidate > conservative_scan_deadline) {
        conservative_scan_deadline = candidate;
    }
}

void initialize_scene_graph_state() noexcept
{
    obs_source_t *current_scene = obs_frontend_get_current_scene();
    scene_graph_stable = current_scene != nullptr;
    if (current_scene != nullptr) {
        obs_source_release(current_scene);
    }
}

bool inspect_source_for_capture_risk(
    void *data, obs_source_t *source) noexcept
{
    auto *scan = static_cast<CaptureScan *>(data);
    if (scan == nullptr || source == nullptr ||
        !chatview::is_capture_risk_source_id(
            obs_source_get_unversioned_id(source))) {
        return true;
    }

    scan->display_capture_present = true;
    if (obs_source_active(source) || obs_source_showing(source)) {
        scan->display_capture_active_or_showing = true;
        return false;
    }
    return true;
}

CaptureScan scan_display_capture_sources() noexcept
{
    CaptureScan scan;
    obs_enum_sources(&inspect_source_for_capture_risk, &scan);
    return scan;
}

void publish_frontend_state() noexcept
{
    if (!runtime_controller) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const bool streaming_pending =
        deadline_active(streaming_start_deadline, now);
    const bool recording_pending =
        deadline_active(recording_start_deadline, now);
    const bool replay_buffer_pending =
        deadline_active(replay_buffer_start_deadline, now);

    const bool streaming = obs_frontend_streaming_active();
    const bool recording = obs_frontend_recording_active();
    const bool replay_buffer = obs_frontend_replay_buffer_active();
    const bool virtual_camera = obs_frontend_virtualcam_active();

    const bool effective_streaming = streaming || streaming_pending;
    const bool effective_recording = recording || recording_pending;
    const bool effective_replay_buffer =
        replay_buffer || replay_buffer_pending;
    const bool output_active =
        effective_streaming || effective_recording ||
        effective_replay_buffer || virtual_camera;

    CaptureScan scan;
    if (scene_graph_stable) {
        scan = scan_display_capture_sources();
    }

    const bool conservative_scan =
        streaming_pending || recording_pending || replay_buffer_pending ||
        deadline_active(conservative_scan_deadline, now);
    const bool display_capture_risk =
        output_active &&
        (!scene_graph_stable ||
         chatview::should_treat_display_capture_as_risk(
             scan.display_capture_present,
             scan.display_capture_active_or_showing,
             conservative_scan));
    const bool suppress = chatview::should_suppress_private_hud(
        display_capture_risk,
        effective_streaming,
        effective_recording,
        effective_replay_buffer,
        virtual_camera);

    if (!published_capture_state || suppress != last_capture_suppressed) {
        if (suppress) {
            blog(
                LOG_WARNING,
                "[ChatView OBS] Private HUD hidden by the Display Capture "
                "safety interlock while an OBS output is starting or running");
        } else if (published_capture_state) {
            blog(
                LOG_INFO,
                "[ChatView OBS] Display Capture interlock cleared; "
                "private HUD restored");
        }
        published_capture_state = true;
        last_capture_suppressed = suppress;
    }

    runtime_controller->update(streaming, recording, suppress);
    if (control_center_bridge) {
        control_center_bridge->update(
            streaming,
            recording,
            replay_buffer,
            virtual_camera,
            suppress,
            scene_graph_stable,
            scan.display_capture_active_or_showing,
            runtime_controller->runtime_telemetry());
    }
}

void CALLBACK capture_risk_timer(
    HWND, UINT, UINT_PTR, DWORD) noexcept
{
    if (control_center_bridge && runtime_controller &&
        control_center_bridge->consume_restart_request() &&
        !runtime_controller->restart_hud()) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] Control Center HUD restart could not be requested");
    }
    publish_frontend_state();
}

void on_frontend_event(obs_frontend_event event, void *) noexcept
{
    bool publish = true;

    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        initialize_scene_graph_state();
        break;

    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
        scene_graph_stable = false;
        break;

    case OBS_FRONTEND_EVENT_STREAMING_STARTING:
        begin_output_start(streaming_start_deadline);
        break;
    case OBS_FRONTEND_EVENT_STREAMING_STARTED:
        complete_output_start(streaming_start_deadline);
        break;
    case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
        streaming_start_deadline = 0U;
        break;

    case OBS_FRONTEND_EVENT_RECORDING_STARTING:
        begin_output_start(recording_start_deadline);
        break;
    case OBS_FRONTEND_EVENT_RECORDING_STARTED:
        complete_output_start(recording_start_deadline);
        break;
    case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
        recording_start_deadline = 0U;
        break;

    case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING:
        begin_output_start(replay_buffer_start_deadline);
        break;
    case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
        complete_output_start(replay_buffer_start_deadline);
        break;
    case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
        replay_buffer_start_deadline = 0U;
        break;

    case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
        complete_output_start(conservative_scan_deadline);
        break;

    case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
    case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
    case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING:
    case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
    case OBS_FRONTEND_EVENT_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
    case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
    case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
    case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
    case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
    case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
        break;

    default:
        publish = false;
        break;
    }

    if (publish) {
        publish_frontend_state();
    }
}

bool launch_sibling_process(const wchar_t *file_name) noexcept
{
    try {
        static int module_anchor = 0;
        HMODULE module_handle = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&module_anchor),
                &module_handle)) {
            return false;
        }

        std::array<wchar_t, 32768U> module_path{};
        const DWORD length = GetModuleFileNameW(
            module_handle,
            module_path.data(),
            static_cast<DWORD>(module_path.size()));
        if (length == 0U || length >= module_path.size()) {
            return false;
        }

        const std::filesystem::path path =
            std::filesystem::path(module_path.data()).parent_path() /
            file_name;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            return false;
        }

        std::wstring command_line = L"\"" + path.wstring() + L"\"";
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
            return false;
        }

        chatview::UniqueHandle process(process_info.hProcess);
        chatview::UniqueHandle thread(process_info.hThread);
        return true;
    } catch (...) {
        return false;
    }
}

void open_settings(void *)
{
    if (control_center_bridge &&
        !control_center_bridge->open_control_center()) {
        blog(LOG_ERROR, "[ChatView OBS] Control Center could not be opened");
    }
}

void export_diagnostics(void *)
{
    if (!launch_sibling_process(kDiagnosticsExecutableName)) {
        blog(LOG_ERROR, "[ChatView OBS] Diagnostics exporter could not be opened");
    }
}

void restart_hud(void *)
{
    if (runtime_controller && !runtime_controller->restart_hud()) {
        blog(LOG_ERROR, "[ChatView OBS] HUD restart could not be requested");
    }
}

void toggle_edit_mode(void *)
{
    if (runtime_controller && !runtime_controller->toggle_edit_mode()) {
        blog(LOG_ERROR, "[ChatView OBS] Overlay edit mode could not be toggled");
    }
}

void reset_frontend_tracking() noexcept
{
    scene_graph_stable = false;
    streaming_start_deadline = 0U;
    recording_start_deadline = 0U;
    replay_buffer_start_deadline = 0U;
    conservative_scan_deadline = 0U;
    published_capture_state = false;
    last_capture_suppressed = false;
}

void disconnect_frontend_callbacks() noexcept
{
    if (capture_risk_timer_id != 0U) {
        KillTimer(nullptr, capture_risk_timer_id);
        capture_risk_timer_id = 0U;
    }
    if (frontend_callback_registered) {
        obs_frontend_remove_event_callback(on_frontend_event, nullptr);
        frontend_callback_registered = false;
    }
}

void destroy_runtime_components() noexcept
{
    if (control_center_bridge) {
        control_center_bridge->stop();
        control_center_bridge.reset();
    }
    if (runtime_controller) {
        runtime_controller->stop();
        runtime_controller.reset();
    }
}

} // namespace

MODULE_EXPORT const char *obs_module_name(void)
{
    return "ChatView OBS";
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Private transparent chat overlay controlled by OBS Studio.";
}

bool obs_module_load(void)
{
    try {
        reset_frontend_tracking();

        runtime_controller =
            std::make_unique<chatview::RuntimeController>();
        if (!runtime_controller->start()) {
            blog(LOG_ERROR, "[ChatView OBS] Failed to initialize HUD runtime");
            runtime_controller.reset();
            return false;
        }

        control_center_bridge =
            std::make_unique<chatview::ControlCenterBridge>();
        if (!control_center_bridge->start()) {
            blog(
                LOG_ERROR,
                "[ChatView OBS] Failed to initialize Control Center bridge");
            destroy_runtime_components();
            return false;
        }

        obs_frontend_add_event_callback(on_frontend_event, nullptr);
        frontend_callback_registered = true;
        initialize_scene_graph_state();

        capture_risk_timer_id = SetTimer(
            nullptr,
            0U,
            kCaptureRiskPollIntervalMs,
            &capture_risk_timer);
        if (capture_risk_timer_id == 0U) {
            blog(
                LOG_ERROR,
                "[ChatView OBS] Failed to start Display Capture safety monitor");
            disconnect_frontend_callbacks();
            destroy_runtime_components();
            return false;
        }

        obs_frontend_add_tools_menu_item(
            obs_module_text("ChatView.RestartHud"), restart_hud, nullptr);
        obs_frontend_add_tools_menu_item(
            obs_module_text("ChatView.EditOverlay"), toggle_edit_mode, nullptr);
        obs_frontend_add_tools_menu_item(
            obs_module_text("ChatView.Diagnostics"), export_diagnostics, nullptr);
        obs_frontend_add_tools_menu_item(
            obs_module_text("ChatView.Settings"), open_settings, nullptr);
        publish_frontend_state();

        blog(LOG_INFO, "[ChatView OBS] Plugin loaded (version %s)", CHATVIEW_VERSION);
        return true;
    } catch (const std::exception &error) {
        blog(LOG_ERROR, "[ChatView OBS] Plugin load failed: %s", error.what());
    } catch (...) {
        blog(LOG_ERROR, "[ChatView OBS] Plugin load failed with an unknown exception");
    }

    disconnect_frontend_callbacks();
    destroy_runtime_components();
    reset_frontend_tracking();
    return false;
}

void obs_module_unload(void)
{
    disconnect_frontend_callbacks();
    destroy_runtime_components();
    reset_frontend_tracking();
    blog(LOG_INFO, "[ChatView OBS] Plugin unloaded");
}
