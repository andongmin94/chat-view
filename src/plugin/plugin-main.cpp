// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/runtime-controller.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <exception>
#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("chat-view-obs", "en-US")

namespace {

std::unique_ptr<chatview::RuntimeController> runtime_controller;

void publish_frontend_state()
{
    if (runtime_controller) {
        runtime_controller->update(
            obs_frontend_streaming_active(), obs_frontend_recording_active());
    }
}

void on_frontend_event(obs_frontend_event event, void *)
{
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
    case OBS_FRONTEND_EVENT_STREAMING_STARTED:
    case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
    case OBS_FRONTEND_EVENT_RECORDING_STARTED:
    case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
        publish_frontend_state();
        break;
    default:
        break;
    }
}

void open_settings(void *)
{
    if (runtime_controller && !runtime_controller->open_settings()) {
        blog(LOG_ERROR, "[ChatView OBS] Settings could not be opened");
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
        runtime_controller = std::make_unique<chatview::RuntimeController>();
        if (!runtime_controller->start()) {
            blog(LOG_ERROR, "[ChatView OBS] Failed to initialize HUD runtime");
            runtime_controller.reset();
            return false;
        }

        obs_frontend_add_event_callback(on_frontend_event, nullptr);
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

    runtime_controller.reset();
    return false;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);

    if (runtime_controller) {
        runtime_controller->stop();
        runtime_controller.reset();
    }

    blog(LOG_INFO, "[ChatView OBS] Plugin unloaded");
}
