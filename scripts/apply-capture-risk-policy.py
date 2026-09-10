from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise RuntimeError(
            f"Expected exactly one anchor in {path}, found {count}: {old[:140]!r}"
        )
    write(path, content.replace(old, new, 1))


capture_policy = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

namespace chatview {

inline constexpr std::string_view kObsDisplayCaptureSourceId =
    "monitor_capture";

[[nodiscard]] constexpr bool is_capture_risk_source_id(
    std::string_view source_id) noexcept
{
    return source_id == kObsDisplayCaptureSourceId;
}

[[nodiscard]] constexpr bool is_capture_risk_source_id(
    const char *source_id) noexcept
{
    return source_id != nullptr &&
           is_capture_risk_source_id(std::string_view(source_id));
}

[[nodiscard]] constexpr bool should_suppress_private_hud(
    bool display_capture_active,
    bool streaming,
    bool recording,
    bool replay_buffer,
    bool virtual_camera) noexcept
{
    return display_capture_active &&
           (streaming || recording || replay_buffer || virtual_camera);
}

static_assert(is_capture_risk_source_id("monitor_capture"));
static_assert(!is_capture_risk_source_id("window_capture"));
static_assert(should_suppress_private_hud(
    true, true, false, false, false));
static_assert(!should_suppress_private_hud(
    true, false, false, false, false));

} // namespace chatview
'''
write("src/plugin/capture-risk-policy.hpp", capture_policy)

capture_policy_test = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/capture-risk-policy.hpp"

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
    if (!chatview::is_capture_risk_source_id("monitor_capture") ||
        chatview::is_capture_risk_source_id("monitor_capture_v2") ||
        chatview::is_capture_risk_source_id("window_capture") ||
        chatview::is_capture_risk_source_id("game_capture") ||
        chatview::is_capture_risk_source_id("") ||
        chatview::is_capture_risk_source_id(
            static_cast<const char *>(nullptr))) {
        return fail("Display Capture source classification was incorrect");
    }

    if (!chatview::should_suppress_private_hud(
            true, true, false, false, false) ||
        !chatview::should_suppress_private_hud(
            true, false, true, false, false) ||
        !chatview::should_suppress_private_hud(
            true, false, false, true, false) ||
        !chatview::should_suppress_private_hud(
            true, false, false, false, true)) {
        return fail("An active OBS output did not engage capture suppression");
    }

    if (chatview::should_suppress_private_hud(
            false, true, true, true, true) ||
        chatview::should_suppress_private_hud(
            true, false, false, false, false)) {
        return fail("Capture suppression engaged without both required sides");
    }

    return 0;
}
'''
write("tests/capture-risk-policy-test.cpp", capture_policy_test)

plugin_main = r'''// SPDX-License-Identifier: GPL-2.0-or-later

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
'''
write("src/plugin/plugin-main.cpp", plugin_main)

replace_once(
    "src/common/shared-state.hpp",
    "inline constexpr std::uint32_t kSharedStateVersion = 3U;",
    "inline constexpr std::uint32_t kSharedStateVersion = 4U;",
)
replace_once(
    "src/common/shared-state.hpp",
    "    SharedStateRecording = 1U << 1U,\n"
    "    SharedStateShutdown = 1U << 31U,\n",
    "    SharedStateRecording = 1U << 1U,\n"
    "    SharedStateCaptureRisk = 1U << 2U,\n"
    "    SharedStateShutdown = 1U << 31U,\n",
)
replace_once(
    "src/common/shared-state.hpp",
    "    SharedStateStreaming | SharedStateRecording | SharedStateShutdown;",
    "    SharedStateStreaming | SharedStateRecording |\n"
    "    SharedStateCaptureRisk | SharedStateShutdown;",
)
replace_once(
    "src/common/shared-state.hpp",
    "static_assert(!are_valid_shared_state_flags(1U << 2U));",
    "static_assert(!are_valid_shared_state_flags(1U << 3U));",
)

replace_once(
    "src/plugin/runtime-controller.hpp",
    "    void update(bool streaming, bool recording) noexcept;",
    "    void update(\n"
    "        bool streaming, bool recording, bool capture_risk) noexcept;",
)

old_update = r'''void RuntimeController::update(bool streaming, bool recording) noexcept
{
    std::uint32_t flags = SharedStateNone;
    if (streaming) {
        flags |= SharedStateStreaming;
    }
    if (recording) {
        flags |= SharedStateRecording;
    }
    current_flags_.store(flags, std::memory_order_release);

    if (stopping_.load(std::memory_order_acquire)) {
        return;
    }

    SharedSrwLockGuard publish_lock(state_publish_lock_);
    if (state_publish_handle_ != nullptr && !SetEvent(state_publish_handle_)) {
        log_windows_error("SetEvent(state publish)", GetLastError());
    }
}
'''
new_update = r'''void RuntimeController::update(
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
'''
replace_once("src/plugin/runtime-controller.cpp", old_update, new_update)

replace_once(
    "src/plugin/runtime-controller.cpp",
    "bool RuntimeController::toggle_edit_mode() noexcept\n{\n"
    "    try {\n"
    "        std::unique_lock lock(mutex_, std::try_to_lock);\n",
    "bool RuntimeController::toggle_edit_mode() noexcept\n{\n"
    "    if ((current_flags_.load(std::memory_order_acquire) &\n"
    "         SharedStateCaptureRisk) != 0U) {\n"
    "        blog(\n"
    "            LOG_WARNING,\n"
    "            \"[ChatView OBS] HUD editing is disabled while the \"\n"
    "            \"Display Capture interlock is active\");\n"
    "        return false;\n"
    "    }\n\n"
    "    try {\n"
    "        std::unique_lock lock(mutex_, std::try_to_lock);\n",
)

replace_once(
    "src/hud/hud-window.hpp",
    "    void fail_closed_capture_exclusion() noexcept;\n"
    "    void update_host_state() noexcept;\n",
    "    void fail_closed_capture_exclusion() noexcept;\n"
    "    bool apply_capture_policy() noexcept;\n"
    "    void update_host_state() noexcept;\n",
)
replace_once(
    "src/hud/hud-window.hpp",
    "    bool recording_ = false;\n"
    "    bool edit_mode_ = false;\n",
    "    bool recording_ = false;\n"
    "    bool capture_risk_ = false;\n"
    "    bool edit_mode_ = false;\n",
)

old_apply_state = r'''void HudWindow::apply_state(const SharedSnapshot &snapshot)
{
    if (snapshot.generation == last_generation_) {
        return;
    }
    last_generation_ = snapshot.generation;

    if (has_flag(snapshot, SharedStateShutdown)) {
        PostQuitMessage(0);
        return;
    }

    const bool was_active = streaming_ || recording_;
    streaming_ = has_flag(snapshot, SharedStateStreaming);
    recording_ = has_flag(snapshot, SharedStateRecording);
    const bool active = streaming_ || recording_;

    if (active) {
        clear_transient_status();
    } else if (was_active) {
        set_transient_status(L"OFFLINE", L"#aeb0b2", kOfflineDurationMs);
        return;
    }
    update_host_state();
}
'''
new_apply_state = r'''void HudWindow::apply_state(const SharedSnapshot &snapshot)
{
    if (snapshot.generation == last_generation_) {
        return;
    }
    last_generation_ = snapshot.generation;

    if (has_flag(snapshot, SharedStateShutdown)) {
        PostQuitMessage(0);
        return;
    }

    const bool was_active = streaming_ || recording_;
    streaming_ = has_flag(snapshot, SharedStateStreaming);
    recording_ = has_flag(snapshot, SharedStateRecording);
    capture_risk_ = has_flag(snapshot, SharedStateCaptureRisk);
    const bool active = streaming_ || recording_;

    if (active) {
        clear_transient_status();
    } else if (was_active) {
        set_transient_status(L"OFFLINE", L"#aeb0b2", kOfflineDurationMs);
    } else {
        update_host_state();
    }

    apply_capture_policy();
}
'''
replace_once("src/hud/hud-window.cpp", old_apply_state, new_apply_state)

old_webview_ready = r'''    case kWebViewReadyMessage:
        webview_ready_ = true;
        reload_chat_config();
        update_host_state();
        if (!capture_exclusion_intact()) {
            fail_closed_capture_exclusion();
            return 0L;
        }
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        if (!SetWindowPos(
                window_,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
            debug_windows_error(L"SetWindowPos(show HUD)");
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(12);
            return 0L;
        }
        if (!capture_exclusion_intact()) {
            fail_closed_capture_exclusion();
            return 0L;
        }
        if (ready_event_ != nullptr && !SetEvent(ready_event_)) {
            debug_windows_error(L"SetEvent(ready)");
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(13);
        }
        return 0L;
'''
new_webview_ready = r'''    case kWebViewReadyMessage:
        webview_ready_ = true;
        reload_chat_config();
        update_host_state();
        if (!capture_exclusion_intact()) {
            fail_closed_capture_exclusion();
            return 0L;
        }
        if (!apply_capture_policy()) {
            return 0L;
        }
        if (ready_event_ != nullptr && !SetEvent(ready_event_)) {
            debug_windows_error(L"SetEvent(ready)");
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(13);
        }
        return 0L;
'''
replace_once("src/hud/hud-window.cpp", old_webview_ready, new_webview_ready)

replace_once(
    "src/hud/hud-window.cpp",
    "    if (window_ == nullptr || !webview_ready_ || capture_exclusion_failed_) {\n"
    "        return;\n"
    "    }\n\n"
    "    if (edit_mode_) {\n",
    "    if (window_ == nullptr || !webview_ready_ ||\n"
    "        capture_exclusion_failed_ || capture_risk_) {\n"
    "        return;\n"
    "    }\n\n"
    "    if (edit_mode_) {\n",
)

replace_once(
    "src/hud/hud-window.cpp",
    "    case WM_CLOSE:\n"
    "        DestroyWindow(window_);\n"
    "        return 0L;\n",
    "    case WM_CLOSE:\n"
    "        ShowWindow(window_, SW_HIDE);\n"
    "        PostQuitMessage(0);\n"
    "        return 0L;\n",
)

capture_policy_method = r'''bool HudWindow::apply_capture_policy() noexcept
{
    if (window_ == nullptr || !webview_ready_ ||
        capture_exclusion_failed_) {
        return !capture_exclusion_failed_;
    }

    if (capture_risk_) {
        if (edit_mode_) {
            capture_and_persist_bounds();
            edit_mode_ = false;
            apply_window_mode();
            update_host_state();
        }
        ShowWindow(window_, SW_HIDE);
        return true;
    }

    if (!capture_exclusion_intact()) {
        fail_closed_capture_exclusion();
        return false;
    }

    if (!SetWindowPos(
            window_,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                SWP_SHOWWINDOW)) {
        debug_windows_error(L"SetWindowPos(show HUD)");
        ShowWindow(window_, SW_HIDE);
        PostQuitMessage(12);
        return false;
    }

    if (!capture_exclusion_intact()) {
        fail_closed_capture_exclusion();
        return false;
    }
    return true;
}

'''
replace_once(
    "src/hud/hud-window.cpp",
    "void HudWindow::update_host_state() noexcept\n",
    capture_policy_method + "void HudWindow::update_host_state() noexcept\n",
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    "bool wait_for_width_greater(HWND window, LONG previous_width) noexcept\n",
    "bool wait_for_visibility(\n"
    "    HWND window, bool visible) noexcept\n"
    "{\n"
    "    const ULONGLONG deadline =\n"
    "        GetTickCount64() + kWindowStateTimeoutMs;\n"
    "    while (GetTickCount64() < deadline) {\n"
    "        if ((IsWindowVisible(window) != FALSE) == visible) {\n"
    "            return true;\n"
    "        }\n"
    "        Sleep(25U);\n"
    "    }\n"
    "    return false;\n"
    "}\n\n"
    "bool wait_for_width_greater(HWND window, LONG previous_width) noexcept\n",
)
replace_once(
    "src/preflight/hud-self-test.cpp",
    "    mapped_state.get()->flags = chatview::SharedStateStreaming;\n",
    "    mapped_state.get()->flags =\n"
    "        chatview::SharedStateStreaming |\n"
    "        chatview::SharedStateCaptureRisk;\n",
)
replace_once(
    "src/preflight/hud-self-test.cpp",
    "    if (!IsWindowVisible(window)) {\n"
    "        return fail(\n"
    "            L\"The ready HUD window was not visible\",\n"
    "            child_process.get());\n"
    "    }\n\n"
    "    constexpr LONG_PTR locked_style =\n",
    "    if (IsWindowVisible(window)) {\n"
    "        return fail(\n"
    "            L\"The HUD ignored capture suppression during startup\",\n"
    "            child_process.get());\n"
    "    }\n\n"
    "    constexpr LONG_PTR locked_style =\n",
)
replace_once(
    "src/preflight/hud-self-test.cpp",
    "    if (!GetWindowDisplayAffinity(window, &affinity) ||\n"
    "        affinity != WDA_EXCLUDEFROMCAPTURE) {\n"
    "        return fail(\n"
    "            L\"The HUD did not request capture exclusion\",\n"
    "            child_process.get());\n"
    "    }\n\n"
    "    const UINT toggle_edit_message =\n",
    "    if (!GetWindowDisplayAffinity(window, &affinity) ||\n"
    "        affinity != WDA_EXCLUDEFROMCAPTURE) {\n"
    "        return fail(\n"
    "            L\"The HUD did not request capture exclusion\",\n"
    "            child_process.get());\n"
    "    }\n\n"
    "    publish(\n"
    "        mapped_state.get(),\n"
    "        state_event.get(),\n"
    "        chatview::SharedStateStreaming);\n"
    "    if (!wait_for_visibility(window, true)) {\n"
    "        return fail(\n"
    "            L\"The HUD did not return after capture risk cleared\",\n"
    "            child_process.get());\n"
    "    }\n\n"
    "    const UINT toggle_edit_message =\n",
)

runtime_suppression_anchor = r'''    if (!wait_for_style(window, locked_style, 0)) {
        return fail(
            L"The HUD did not return to locked mode",
            child_process.get());
    }

    const std::filesystem::path placement_file =
'''
runtime_suppression_replacement = r'''    if (!wait_for_style(window, locked_style, 0)) {
        return fail(
            L"The HUD did not return to locked mode",
            child_process.get());
    }

    publish(
        mapped_state.get(),
        state_event.get(),
        chatview::SharedStateStreaming |
            chatview::SharedStateCaptureRisk);
    if (!wait_for_visibility(window, false)) {
        return fail(
            L"The HUD remained visible after capture risk appeared",
            child_process.get());
    }
    if (WaitForSingleObject(child_process.get(), 0U) != WAIT_TIMEOUT) {
        return fail(
            L"Capture suppression terminated the HUD instead of hiding it");
    }
    if (!PostMessageW(window, toggle_edit_message, 0U, 0L)) {
        return fail(
            L"Failed to test editing during capture suppression",
            child_process.get());
    }
    Sleep(150U);
    if (IsWindowVisible(window) ||
        !wait_for_style(window, locked_style, 0)) {
        return fail(
            L"Capture suppression allowed the HUD to enter edit mode",
            child_process.get());
    }

    publish(
        mapped_state.get(),
        state_event.get(),
        chatview::SharedStateStreaming);
    if (!wait_for_visibility(window, true)) {
        return fail(
            L"The HUD did not resume after runtime capture suppression",
            child_process.get());
    }

    const std::filesystem::path placement_file =
'''
replace_once(
    "src/preflight/hud-self-test.cpp",
    runtime_suppression_anchor,
    runtime_suppression_replacement,
)

replace_once(
    "tests/shared-state-reader-test.cpp",
    "        chatview::SharedStateStreaming |\n"
    "            chatview::SharedStateRecording,\n",
    "        chatview::SharedStateStreaming |\n"
    "            chatview::SharedStateRecording |\n"
    "            chatview::SharedStateCaptureRisk,\n",
)
replace_once(
    "tests/shared-state-reader-test.cpp",
    "            (chatview::SharedStateStreaming |\n"
    "             chatview::SharedStateRecording) ||\n",
    "            (chatview::SharedStateStreaming |\n"
    "             chatview::SharedStateRecording |\n"
    "             chatview::SharedStateCaptureRisk) ||\n",
)
replace_once(
    "tests/shared-state-reader-test.cpp",
    "    publish(mapped.get(), 1U << 2U, 43U);\n",
    "    publish(mapped.get(), 1U << 3U, 43U);\n",
)

replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.2.8 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.2.9 LANGUAGES CXX)",
)
replace_once(
    "CMakeLists.txt",
    "add_library(chat-view-obs MODULE\n"
    "    src/plugin/plugin-main.cpp\n",
    "add_library(chat-view-obs MODULE\n"
    "    src/plugin/plugin-main.cpp\n"
    "    src/plugin/capture-risk-policy.hpp\n",
)
replace_once(
    "CMakeLists.txt",
    "    add_executable(chat-view-restart-policy-test\n"
    "        tests/restart-policy-test.cpp\n"
    "        src/plugin/restart-policy.hpp\n"
    "    )\n"
    "    target_include_directories(\n"
    "        chat-view-restart-policy-test PRIVATE \"${CHATVIEW_SOURCE_DIR}\")\n"
    "    chatview_enable_warnings(chat-view-restart-policy-test)\n"
    "    add_test(\n"
    "        NAME chat-view-restart-policy\n"
    "        COMMAND chat-view-restart-policy-test\n"
    "    )\n\n",
    "    add_executable(chat-view-restart-policy-test\n"
    "        tests/restart-policy-test.cpp\n"
    "        src/plugin/restart-policy.hpp\n"
    "    )\n"
    "    target_include_directories(\n"
    "        chat-view-restart-policy-test PRIVATE \"${CHATVIEW_SOURCE_DIR}\")\n"
    "    chatview_enable_warnings(chat-view-restart-policy-test)\n"
    "    add_test(\n"
    "        NAME chat-view-restart-policy\n"
    "        COMMAND chat-view-restart-policy-test\n"
    "    )\n\n"
    "    add_executable(chat-view-capture-risk-policy-test\n"
    "        tests/capture-risk-policy-test.cpp\n"
    "        src/plugin/capture-risk-policy.hpp\n"
    "    )\n"
    "    target_include_directories(\n"
    "        chat-view-capture-risk-policy-test PRIVATE \"${CHATVIEW_SOURCE_DIR}\")\n"
    "    chatview_enable_warnings(chat-view-capture-risk-policy-test)\n"
    "    add_test(\n"
    "        NAME chat-view-capture-risk-policy\n"
    "        COMMAND chat-view-capture-risk-policy-test\n"
    "    )\n\n",
)

replace_once(
    "src/hud/webview-host.cpp",
    "  const restoreHostShell = () => {\n"
    "    host.removeAttribute('hidden');\n"
    "    host.style.setProperty('all', 'initial', 'important');\n"
    "    host.style.setProperty('position', 'fixed', 'important');\n"
    "    host.style.setProperty('inset', '0', 'important');\n"
    "    host.style.setProperty('display', 'block', 'important');\n"
    "    host.style.setProperty('visibility', 'visible', 'important');\n"
    "    host.style.setProperty('opacity', '1', 'important');\n"
    "    host.style.setProperty('z-index', '2147483647', 'important');\n"
    "    host.style.setProperty('pointer-events', 'none', 'important');\n"
    "  };\n\n"
    "  let observer = null;\n",
    "  const expectedHostStyle = (() => {\n"
    "    const probe = document.createElement('div');\n"
    "    probe.style.cssText =\n"
    "      'all:initial!important;position:fixed!important;inset:0!important;' +\n"
    "      'display:block!important;visibility:visible!important;' +\n"
    "      'opacity:1!important;z-index:2147483647!important;' +\n"
    "      'pointer-events:none!important;';\n"
    "    return probe.style.cssText;\n"
    "  })();\n\n"
    "  const restoreHostShell = () => {\n"
    "    if (host.hasAttribute('hidden')) host.removeAttribute('hidden');\n"
    "    if (host.style.cssText !== expectedHostStyle) {\n"
    "      host.style.cssText = expectedHostStyle;\n"
    "    }\n"
    "  };\n\n"
    "  let observer = null;\n"
    "  let observedStyleParent = null;\n"
    "  let pageStyleObserved = false;\n",
)
replace_once(
    "src/hud/webview-host.cpp",
    "      if (observer) {\n"
    "        observer.observe(styleParent, { childList: true });\n"
    "      }\n",
    "      if (observer && observedStyleParent !== styleParent) {\n"
    "        observer.observe(styleParent, { childList: true });\n"
    "        observedStyleParent = styleParent;\n"
    "      }\n"
    "      if (observer && !pageStyleObserved) {\n"
    "        observer.observe(pageStyle, {\n"
    "          childList: true,\n"
    "          characterData: true,\n"
    "          subtree: true\n"
    "        });\n"
    "        pageStyleObserved = true;\n"
    "      }\n",
)
replace_once(
    "src/hud/webview-host.cpp",
    "  if (document.documentElement) {\n"
    "    observer.observe(document.documentElement, { childList: true });\n"
    "  }\n",
    "  observer.observe(document, { childList: true });\n"
    "  if (document.documentElement) {\n"
    "    observer.observe(document.documentElement, { childList: true });\n"
    "  }\n",
)

readme = read("README.md")
status_anchor = (
    "- Windows capture-exclusion request through `WDA_EXCLUDEFROMCAPTURE`;\n"
)
if status_anchor not in readme:
    raise RuntimeError("README capture bullet not found")
readme = readme.replace(
    status_anchor,
    status_anchor
    + "- fail-closed Display Capture interlock while streaming, recording, replay buffering, or virtual-camera output is active;\n",
    1,
)
wda_anchor = (
    "`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint. "
    "It does not remove the HUD from a physical HDMI signal sent to a capture card."
)
if wda_anchor not in readme:
    raise RuntimeError("README WDA paragraph not found")
readme = readme.replace(
    wda_anchor,
    wda_anchor
    + " As a second software-side barrier, ChatView hides the private HUD whenever OBS reports an active Display Capture source while streaming, recording, replay buffering, or virtual-camera output is running. The HUD returns only after that condition clears.",
    1,
)
write("README.md", readme)

architecture = read("docs/architecture.md")
architecture = architecture.replace(
    "The single-PC transport uses a named Windows file mapping and an auto-reset event. Protocol version 3 carries only:",
    "The single-PC transport uses a named Windows file mapping and an auto-reset event. Protocol version 4 carries only:",
    1,
)
architecture = architecture.replace(
    "- recording active;\n"
    "- shutdown requested;\n",
    "- recording active;\n"
    "- private-HUD capture suppression active;\n"
    "- shutdown requested;\n",
    1,
)
desktop_anchor = (
    "`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint, not DRM and not an HDMI-path guarantee. "
    "A future broadcast-visible overlay must be a separate OBS source rather than weakening the private-HUD default."
)
if desktop_anchor not in architecture:
    raise RuntimeError("Architecture WDA paragraph not found")
architecture = architecture.replace(
    desktop_anchor,
    desktop_anchor
    + "\n\nThe plugin also runs a 100 ms output-safety monitor. It enumerates active OBS input sources and recognizes the pinned Windows Display Capture source ID `monitor_capture`. If Display Capture is active while streaming, recording, replay buffering, or virtual-camera output is running, protocol version 4 orders the HUD to hide. Output-starting events perform an immediate scan, an unstable scene/profile graph is treated as risky, edit mode is disabled during suppression, and the HUD reappears only after the risk clears. This interlock is defense in depth above the Windows affinity check; it is not presented as protection for physical capture-card paths."
    + "\n\nThe repair observer for the injected control shell uses normalized, compare-before-write inline state and watches the document root, host attributes, and transparency style. Its own repairs therefore settle instead of continuously triggering itself.",
    1,
)
testing_anchor = (
    "- deterministic restart-policy tests covering backoff, circuit opening, saturation, and explicit reset;\n"
)
if testing_anchor not in architecture:
    raise RuntimeError("Architecture testing anchor not found")
architecture = architecture.replace(
    testing_anchor,
    testing_anchor
    + "- capture-risk policy tests plus startup and runtime HUD hide/resume checks through the versioned shared-state transport;\n",
    1,
)
write("docs/architecture.md", architecture)

for required in (
    "SharedStateCaptureRisk",
    "kSharedStateVersion = 4U",
):
    if required not in read("src/common/shared-state.hpp"):
        raise RuntimeError(f"Missing shared-state marker: {required}")

for required in (
    "obs_frontend_replay_buffer_active",
    "obs_frontend_virtualcam_active",
    "SetTimer(",
    "active_display_capture_present",
):
    if required not in read("src/plugin/plugin-main.cpp"):
        raise RuntimeError(f"Missing plugin capture monitor marker: {required}")

for required in (
    "apply_capture_policy",
    "capture_risk_",
):
    if required not in read("src/hud/hud-window.cpp") + read("src/hud/hud-window.hpp"):
        raise RuntimeError(f"Missing HUD capture-policy marker: {required}")

webview = read("src/hud/webview-host.cpp")
if "host.style.setProperty('all'" in webview:
    raise RuntimeError("The observer still rewrites host styles unconditionally")
if "observer.observe(document, { childList: true });" not in webview:
    raise RuntimeError("The control shell does not observe document-root replacement")

for obsolete in (
    ".github/workflows/apply-capture-risk-policy.yml",
    "scripts/apply-capture-risk-policy.py",
):
    target = ROOT / obsolete
    if target.exists():
        target.unlink()
