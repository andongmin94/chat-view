// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/capture-risk-policy.hpp"
#include "plugin/runtime-controller.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <Windows.h>

#include <exception>
#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("chat-view-obs", "en-US")

namespace {

constexpr UINT kCaptureRiskPollIntervalMs = 100U;

struct CaptureScan {
    bool display_capture_active = false;
};

std::unique_ptr<chatview::RuntimeController> runtime_controller;
UINT_PTR capture_risk_timer_id = 0U;
bool frontend_callback_registered = false;
bool scene_graph_stable = false;
bool streaming_starting = false;
bool recording_starting = false;
bool replay_buffer_starting = false;
bool published_capture_state = false;
bool last_capture_suppressed = false;

bool inspect_source_for_capture_risk(
    void *data, obs_source_t *source) noexcept
{
    auto *scan = static_cast<CaptureScan *>(data);
    if (scan == nullptr || source == nullptr) {
        return true;
    }

    if (obs_source_active(source) &&
        chatview::is_capture_risk_source_id(
            obs_source_get_unversioned_id(source))) {
        scan->display_capture_active = true;
        return false;
    }
    return true;
}

bool active_display_capture_present() noexcept
{
    CaptureScan scan;
    obs_enum_sources(&inspect_source_for_capture_risk, &scan);
    return scan.display_capture_active;
}

void publish_frontend_state() noexcept
{
    if (!runtime_controller) {
        return;
    }

    const bool streaming =
        streaming_starting || obs_frontend_streaming_active();
    const bool recording =
        recording_starting || obs_frontend_recording_active();
    const bool replay_buffer =
        replay_buffer_starting || obs_frontend_replay_buffer_active();
    const bool virtual_camera = obs_frontend_virtualcam_active();

    const bool output_active =
        streaming || recording || replay_buffer || virtual_camera;
    const bool display_capture_active =
        output_active &&
        (!scene_graph_stable || active_display_capture_present());
    const bool suppress = chatview::should_suppress_private_hud(
        display_capture_active,
        streaming,
        recording,
        replay_buffer,
        virtual_camera);

    if (!published_capture_state || suppress != last_capture_suppressed) {
        if (suppress) {
            blog(
                LOG_WARNING,
                "[ChatView OBS] Private HUD hidden by the Display Capture "
                "safety interlock while an OBS output is running");
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
}

void CALLBACK capture_risk_timer(
    HWND, UINT, UINT_PTR, DWORD) noexcept
{
    publish_frontend_state();
}

void on_frontend_event(obs_frontend_event event, void *) noexcept
{
    bool publish = true;

    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        scene_graph_stable = true;
        break;

    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
        scene_graph_stable = false;
        break;

    case OBS_FRONTEND_EVENT_STREAMING_STARTING:
        streaming_starting = true;
        break;
    case OBS_FRONTEND_EVENT_STREAMING_STARTED:
    case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
        streaming_starting = false;
        break;

    case OBS_FRONTEND_EVENT_RECORDING_STARTING:
        recording_starting = true;
        break;
    case OBS_FRONTEND_EVENT_RECORDING_STARTED:
    case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
        recording_starting = false;
        break;

    case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING:
        replay_buffer_starting = true;
        break;
    case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
    case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
        replay_buffer_starting = false;
        break;

    case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
    case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
    case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING:
    case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
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

void open_settings(void *)
{
    if (runtime_controller && !runtime_controller->open_settings()) {
        blog(LOG_ERROR, "[ChatView OBS] Settings could not be opened");
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
    streaming_starting = false;
    recording_starting = false;
    replay_buffer_starting = false;
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

        runtime_controller = std::make_unique<chatview::RuntimeController>();
        if (!runtime_controller->start()) {
            blog(LOG_ERROR, "[ChatView OBS] Failed to initialize HUD runtime");
            runtime_controller.reset();
            return false;
        }

        obs_frontend_add_event_callback(on_frontend_event, nullptr);
        frontend_callback_registered = true;

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
            runtime_controller->stop();
            runtime_controller.reset();
            return false;
        }

        obs_frontend_add_tools_menu_item(
            obs_module_text("ChatView.RestartHud"), restart_hud, nullptr);
        obs_frontend_add_tools_menu_item(
            obs_module_text("ChatView.EditOverlay"), toggle_edit_mode, nullptr);
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
    if (runtime_controller) {
        runtime_controller->stop();
        runtime_controller.reset();
    }
    reset_frontend_tracking();
    return false;
}

void obs_module_unload(void)
{
    disconnect_frontend_callbacks();

    if (runtime_controller) {
        runtime_controller->stop();
        runtime_controller.reset();
    }

    reset_frontend_tracking();
    blog(LOG_INFO, "[ChatView OBS] Plugin unloaded");
}
