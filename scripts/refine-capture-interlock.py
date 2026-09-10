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
constexpr ULONGLONG kOutputStartPendingTimeoutMs = 15000U;
constexpr ULONGLONG kPostStartConservativeScanMs = 1000U;

struct CaptureScan {
    bool display_capture_present = false;
    bool display_capture_active_or_showing = false;
};

std::unique_ptr<chatview::RuntimeController> runtime_controller;
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
    if (output_active && scene_graph_stable) {
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

[[nodiscard]] constexpr bool should_treat_display_capture_as_risk(
    bool display_capture_present,
    bool display_capture_active_or_showing,
    bool conservative_scan) noexcept
{
    return conservative_scan
               ? display_capture_present
               : display_capture_active_or_showing;
}

[[nodiscard]] constexpr bool should_suppress_private_hud(
    bool display_capture_risk,
    bool streaming,
    bool recording,
    bool replay_buffer,
    bool virtual_camera) noexcept
{
    return display_capture_risk &&
           (streaming || recording || replay_buffer || virtual_camera);
}

static_assert(is_capture_risk_source_id("monitor_capture"));
static_assert(!is_capture_risk_source_id("window_capture"));
static_assert(should_treat_display_capture_as_risk(
    true, false, true));
static_assert(!should_treat_display_capture_as_risk(
    true, false, false));
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

    if (!chatview::should_treat_display_capture_as_risk(
            true, false, true) ||
        !chatview::should_treat_display_capture_as_risk(
            true, true, false) ||
        chatview::should_treat_display_capture_as_risk(
            false, true, true) ||
        chatview::should_treat_display_capture_as_risk(
            true, false, false)) {
        return fail("Conservative Display Capture classification was incorrect");
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

replace_once(
    "src/hud/webview-host.cpp",
    "  const defaultTone = '#aeb0b2';\n"
    "  const host = document.createElement('chatview-private-hud-root');\n",
    "  const nativeBridge = window.chrome.webview;\n"
    "  const defaultTone = '#aeb0b2';\n"
    "  const host = document.createElement('chatview-private-hud-root');\n",
)
replace_once(
    "src/hud/webview-host.cpp",
    "  window.chrome.webview.addEventListener('message', (event) => {\n"
    "    applyState(event.data);\n"
    "  });\n",
    "  nativeBridge.addEventListener('message', (event) => {\n"
    "    if (!event.isTrusted || event.source !== nativeBridge) return;\n"
    "    applyState(event.data);\n"
    "  });\n",
)

architecture = read("docs/architecture.md")
replace_text = (
    "OBS state reaches that closed control layer only through native-to-page JSON messages. "
    "The provider page receives no callable ChatView control function, and the native host registers no page-to-native message handler."
)
if replace_text not in architecture:
    raise RuntimeError("WebView boundary paragraph was not found")
architecture = architecture.replace(
    replace_text,
    replace_text
    + " The receiver also requires a trusted WebView2 message event whose source is the captured native bridge object, so script-dispatched lookalike events are ignored.",
    1,
)
old_interlock = (
    "The plugin also runs a 100 ms output-safety monitor. It enumerates active OBS input sources and recognizes the pinned Windows Display Capture source ID `monitor_capture`. "
    "If Display Capture is active while streaming, recording, replay buffering, or virtual-camera output is running, protocol version 4 orders the HUD to hide. "
    "Output-starting events perform an immediate scan, an unstable scene/profile graph is treated as risky, edit mode is disabled during suppression, and the HUD reappears only after the risk clears."
)
if old_interlock not in architecture:
    raise RuntimeError("Capture interlock paragraph was not found")
architecture = architecture.replace(
    old_interlock,
    "The plugin also runs a 100 ms output-safety monitor. It enumerates OBS input sources and recognizes the pinned Windows Display Capture source ID `monitor_capture`. "
    "If Display Capture is active or showing while streaming, recording, replay buffering, or virtual-camera output is running, protocol version 4 orders the HUD to hide. "
    "Output-starting events use a bounded pending state and temporarily treat any configured Display Capture source as risky, closing the gap before OBS activates the program source. "
    "A post-start conservative interval covers OBS events emitted before source activation, failed starts expire instead of leaving the HUD permanently suppressed, an unstable scene/profile graph is treated as risky, edit mode is disabled during suppression, and the HUD reappears only after the risk clears.",
    1,
)
write("docs/architecture.md", architecture)

readme = read("README.md")
old_readme = (
    "As a second software-side barrier, ChatView hides the private HUD whenever OBS reports an active Display Capture source while streaming, recording, replay buffering, or virtual-camera output is running. "
    "The HUD returns only after that condition clears."
)
if old_readme not in readme:
    raise RuntimeError("README interlock paragraph was not found")
readme = readme.replace(
    old_readme,
    "As a second software-side barrier, ChatView hides the private HUD whenever OBS reports an active or showing Display Capture source while streaming, recording, replay buffering, or virtual-camera output is running. "
    "At output start it temporarily treats any configured Display Capture source as risky until OBS source activation settles. Failed starts expire automatically, and the HUD returns only after the risk condition clears.",
    1,
)
write("README.md", readme)

for required in (
    "obs_source_showing(source)",
    "kOutputStartPendingTimeoutMs",
    "initialize_scene_graph_state",
    "should_treat_display_capture_as_risk",
):
    if required not in read("src/plugin/plugin-main.cpp"):
        raise RuntimeError(f"Missing capture refinement marker: {required}")

webview = read("src/hud/webview-host.cpp")
for required in (
    "const nativeBridge = window.chrome.webview;",
    "!event.isTrusted || event.source !== nativeBridge",
):
    if required not in webview:
        raise RuntimeError(f"Missing trusted WebView event marker: {required}")

for obsolete in (
    ".github/workflows/refine-capture-interlock.yml",
    "scripts/refine-capture-interlock.py",
):
    target = ROOT / obsolete
    if target.exists():
        target.unlink()
