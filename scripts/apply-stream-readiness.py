from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def insert_after(path_text: str, anchor: str, addition: str) -> None:
    replace_once(path_text, anchor, anchor + addition)


Path("src/common/stream-readiness.hpp").write_text(
    r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/control-status.hpp"
#include "common/hud-health.hpp"

#include <cstdint>

namespace chatview {

enum class StreamReadinessBlocker : std::uint8_t {
    None = 0U,
    ObsDisconnected,
    StatusUnavailable,
    ChatNotConfigured,
    RestartCircuitOpen,
    SceneGraphUnavailable,
    DisplayCaptureActive,
    HudNotRunning,
    HudHealthUnavailable,
    ChatStarting,
    ChatLoading,
    ChatRetrying,
    ChatRecovering,
    LoginRequired,
    BroadcastOffline,
    LayoutChanged,
    NetworkOffline,
    ConnectionLost,
    SystemPaused,
    SystemResuming,
    ChatFatal,
    HudHidden,
};

struct StreamReadinessInput {
    bool obs_connected = false;
    bool status_available = false;
    bool chat_configured = false;
    bool hud_health_available = false;
    ControlStatusSnapshot status;
    HudHealthSnapshot hud_health;
};

struct StreamReadinessResult {
    bool ready = false;
    StreamReadinessBlocker blocker =
        StreamReadinessBlocker::StatusUnavailable;
};

[[nodiscard]] constexpr StreamReadinessResult evaluate_stream_readiness(
    const StreamReadinessInput &input) noexcept
{
    if (!input.obs_connected) {
        return {false, StreamReadinessBlocker::ObsDisconnected};
    }
    if (!input.status_available) {
        return {false, StreamReadinessBlocker::StatusUnavailable};
    }
    if (!input.chat_configured) {
        return {false, StreamReadinessBlocker::ChatNotConfigured};
    }
    if (has_runtime_telemetry_flag(
            input.status.runtime_telemetry,
            RuntimeTelemetryCircuitOpen)) {
        return {false, StreamReadinessBlocker::RestartCircuitOpen};
    }
    if (!has_control_status_flag(
            input.status, ControlStatusSceneGraphReady)) {
        return {false, StreamReadinessBlocker::SceneGraphUnavailable};
    }
    if (has_control_status_flag(
            input.status, ControlStatusDisplayCaptureActive) ||
        has_control_status_flag(
            input.status, ControlStatusCaptureRisk)) {
        return {false, StreamReadinessBlocker::DisplayCaptureActive};
    }
    if (!has_control_status_flag(
            input.status, ControlStatusHudRunning)) {
        return {false, StreamReadinessBlocker::HudNotRunning};
    }
    if (!input.hud_health_available ||
        input.hud_health.provider == HudProvider::Unknown) {
        return {false, StreamReadinessBlocker::HudHealthUnavailable};
    }

    switch (input.hud_health.state) {
    case HudPageState::Ready:
        break;
    case HudPageState::SetupRequired:
        return {false, StreamReadinessBlocker::ChatNotConfigured};
    case HudPageState::Starting:
        return {false, StreamReadinessBlocker::ChatStarting};
    case HudPageState::Loading:
        return {false, StreamReadinessBlocker::ChatLoading};
    case HudPageState::Retrying:
        return {false, StreamReadinessBlocker::ChatRetrying};
    case HudPageState::Recovering:
        return {false, StreamReadinessBlocker::ChatRecovering};
    case HudPageState::LoginRequired:
        return {false, StreamReadinessBlocker::LoginRequired};
    case HudPageState::Offline:
        return {false, StreamReadinessBlocker::BroadcastOffline};
    case HudPageState::LayoutChanged:
        return {false, StreamReadinessBlocker::LayoutChanged};
    case HudPageState::NetworkOffline:
        return {false, StreamReadinessBlocker::NetworkOffline};
    case HudPageState::ConnectionLost:
        return {false, StreamReadinessBlocker::ConnectionLost};
    case HudPageState::SystemPaused:
        return {false, StreamReadinessBlocker::SystemPaused};
    case HudPageState::SystemResuming:
        return {false, StreamReadinessBlocker::SystemResuming};
    case HudPageState::Fatal:
        return {false, StreamReadinessBlocker::ChatFatal};
    case HudPageState::Unknown:
    default:
        return {false, StreamReadinessBlocker::HudHealthUnavailable};
    }

    if (!has_control_status_flag(
            input.status, ControlStatusHudVisible)) {
        return {false, StreamReadinessBlocker::HudHidden};
    }
    return {true, StreamReadinessBlocker::None};
}

static_assert(
    evaluate_stream_readiness(StreamReadinessInput{}).blocker ==
    StreamReadinessBlocker::ObsDisconnected);

} // namespace chatview
''',
    encoding="utf-8",
    newline="\n",
)

Path("tests/stream-readiness-test.cpp").write_text(
    r'''// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/stream-readiness.hpp"

#include <array>
#include <iostream>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

chatview::StreamReadinessInput ready_input()
{
    chatview::StreamReadinessInput input;
    input.obs_connected = true;
    input.status_available = true;
    input.chat_configured = true;
    input.hud_health_available = true;
    input.status.flags =
        chatview::ControlStatusSceneGraphReady |
        chatview::ControlStatusHudRunning |
        chatview::ControlStatusHudVisible;
    input.status.hud_process_id = 42U;
    input.hud_health = {
        chatview::HudPageState::Ready,
        chatview::HudProvider::YouTube,
        0U};
    return input;
}

bool expect_blocker(
    chatview::StreamReadinessInput input,
    chatview::StreamReadinessBlocker expected)
{
    const chatview::StreamReadinessResult result =
        chatview::evaluate_stream_readiness(input);
    return !result.ready && result.blocker == expected;
}

} // namespace

int main()
{
    const chatview::StreamReadinessInput ready = ready_input();
    const chatview::StreamReadinessResult ready_result =
        chatview::evaluate_stream_readiness(ready);
    if (!ready_result.ready ||
        ready_result.blocker != chatview::StreamReadinessBlocker::None) {
        return fail("A fully ready private HUD was blocked");
    }

    auto input = ready;
    input.obs_connected = false;
    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::ObsDisconnected)) {
        return fail("A disconnected OBS bridge was not blocked");
    }

    input = ready;
    input.status_available = false;
    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::StatusUnavailable)) {
        return fail("Missing OBS status was not blocked");
    }

    input = ready;
    input.chat_configured = false;
    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::ChatNotConfigured)) {
        return fail("A missing chat configuration was not blocked");
    }

    input = ready;
    input.status.runtime_telemetry = {
        2U,
        chatview::RuntimeRestartReason::WebViewFailure,
        chatview::kMaximumRuntimeFailureCount,
        chatview::RuntimeTelemetryHistoryValid |
            chatview::RuntimeTelemetryAutomatic |
            chatview::RuntimeTelemetryCircuitOpen,
        1U};
    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::RestartCircuitOpen)) {
        return fail("An open restart circuit was not blocked");
    }

    input = ready;
    input.status.flags &= ~static_cast<std::uint32_t>(
        chatview::ControlStatusSceneGraphReady);
    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::SceneGraphUnavailable)) {
        return fail("An unavailable scene graph was not blocked");
    }

    input = ready;
    input.status.flags |= chatview::ControlStatusDisplayCaptureActive;
    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::DisplayCaptureActive)) {
        return fail("Active Display Capture was not blocked");
    }

    input = ready;
    input.status.flags &= ~static_cast<std::uint32_t>(
        chatview::ControlStatusHudRunning |
        chatview::ControlStatusHudVisible);
    input.status.hud_process_id = 0U;
    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::HudNotRunning)) {
        return fail("A missing HUD process was not blocked");
    }

    input = ready;
    input.hud_health_available = false;
    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::HudHealthUnavailable)) {
        return fail("Missing HUD health was not blocked");
    }

    struct HealthCase {
        chatview::HudPageState state;
        chatview::StreamReadinessBlocker blocker;
    };
    constexpr std::array<HealthCase, 13U> cases{{
        {chatview::HudPageState::Starting,
         chatview::StreamReadinessBlocker::ChatStarting},
        {chatview::HudPageState::Loading,
         chatview::StreamReadinessBlocker::ChatLoading},
        {chatview::HudPageState::Retrying,
         chatview::StreamReadinessBlocker::ChatRetrying},
        {chatview::HudPageState::Recovering,
         chatview::StreamReadinessBlocker::ChatRecovering},
        {chatview::HudPageState::LoginRequired,
         chatview::StreamReadinessBlocker::LoginRequired},
        {chatview::HudPageState::Offline,
         chatview::StreamReadinessBlocker::BroadcastOffline},
        {chatview::HudPageState::LayoutChanged,
         chatview::StreamReadinessBlocker::LayoutChanged},
        {chatview::HudPageState::NetworkOffline,
         chatview::StreamReadinessBlocker::NetworkOffline},
        {chatview::HudPageState::ConnectionLost,
         chatview::StreamReadinessBlocker::ConnectionLost},
        {chatview::HudPageState::SystemPaused,
         chatview::StreamReadinessBlocker::SystemPaused},
        {chatview::HudPageState::SystemResuming,
         chatview::StreamReadinessBlocker::SystemResuming},
        {chatview::HudPageState::Fatal,
         chatview::StreamReadinessBlocker::ChatFatal},
        {chatview::HudPageState::Unknown,
         chatview::StreamReadinessBlocker::HudHealthUnavailable},
    }};
    for (const HealthCase &health_case : cases) {
        input = ready;
        input.hud_health.state = health_case.state;
        if (!expect_blocker(input, health_case.blocker)) {
            return fail("A non-ready chat health state was misclassified");
        }
    }

    input = ready;
    input.status.flags &= ~static_cast<std::uint32_t>(
        chatview::ControlStatusHudVisible);
    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::HudHidden)) {
        return fail("A hidden HUD was not blocked");
    }

    return 0;
}
''',
    encoding="utf-8",
    newline="\n",
)

replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.3.9 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.4.0 LANGUAGES CXX)",
)
insert_after(
    "CMakeLists.txt",
    '''    add_test(
        NAME chat-view-runtime-telemetry
        COMMAND chat-view-runtime-telemetry-test
    )
''',
    '''
    add_executable(chat-view-stream-readiness-test
        tests/stream-readiness-test.cpp
        src/common/stream-readiness.hpp
    )
    target_include_directories(
        chat-view-stream-readiness-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_win32(chat-view-stream-readiness-test)
    chatview_enable_warnings(chat-view-stream-readiness-test)
    add_test(
        NAME chat-view-stream-readiness
        COMMAND chat-view-stream-readiness-test
    )
''',
)

replace_once(
    "src/common/control-status.hpp",
    "inline constexpr std::uint32_t kControlStatusVersion = 2U;",
    "inline constexpr std::uint32_t kControlStatusVersion = 3U;",
)
replace_once(
    "src/common/control-status.hpp",
    '''    ControlStatusHudRunning = 1U << 5U,
    ControlStatusHudVisible = 1U << 6U,
};''',
    '''    ControlStatusHudRunning = 1U << 5U,
    ControlStatusHudVisible = 1U << 6U,
    ControlStatusSceneGraphReady = 1U << 7U,
    ControlStatusDisplayCaptureActive = 1U << 8U,
};''',
)
replace_once(
    "src/common/control-status.hpp",
    '''    ControlStatusCaptureRisk | ControlStatusHudRunning |
    ControlStatusHudVisible;''',
    '''    ControlStatusCaptureRisk | ControlStatusHudRunning |
    ControlStatusHudVisible | ControlStatusSceneGraphReady |
    ControlStatusDisplayCaptureActive;''',
)
replace_once(
    "src/common/control-status.hpp",
    "static_assert(!are_valid_control_status_flags(1U << 7U));",
    "static_assert(!are_valid_control_status_flags(1U << 9U));",
)

replace_once(
    "src/plugin/control-center-bridge.hpp",
    '''        bool virtual_camera,
        bool capture_risk,
        const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept;''',
    '''        bool virtual_camera,
        bool capture_risk,
        bool scene_graph_ready,
        bool display_capture_active,
        const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept;''',
)
replace_once(
    "src/plugin/control-center-bridge.cpp",
    '''    bool virtual_camera,
    bool capture_risk,
    const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept''',
    '''    bool virtual_camera,
    bool capture_risk,
    bool scene_graph_ready,
    bool display_capture_active,
    const RuntimeTelemetrySnapshot &runtime_telemetry) noexcept''',
)
insert_after(
    "src/plugin/control-center-bridge.cpp",
    '''    if (capture_risk) {
        flags |= ControlStatusCaptureRisk;
    }
''',
    '''    if (scene_graph_ready) {
        flags |= ControlStatusSceneGraphReady;
    }
    if (display_capture_active) {
        flags |= ControlStatusDisplayCaptureActive;
    }
''',
)

replace_once(
    "src/plugin/plugin-main.cpp",
    '''    CaptureScan scan;
    if (output_active && scene_graph_stable) {
        scan = scan_display_capture_sources();
    }
''',
    '''    CaptureScan scan;
    if (scene_graph_stable) {
        scan = scan_display_capture_sources();
    }
''',
)
replace_once(
    "src/plugin/plugin-main.cpp",
    '''            virtual_camera,
            suppress,
            runtime_controller->runtime_telemetry());''',
    '''            virtual_camera,
            suppress,
            scene_graph_stable,
            scan.display_capture_active_or_showing,
            runtime_controller->runtime_telemetry());''',
)

insert_after(
    "src/config/main.cpp",
    '#include "common/hud-health.hpp"\n',
    '#include "common/stream-readiness.hpp"\n',
)
insert_after(
    "src/config/main.cpp",
    '''struct RuntimePresentation {
    std::wstring text;
    COLORREF color = kColorMuted;
};
''',
    '''
struct ReadinessPresentation {
    std::wstring text;
    COLORREF color = kColorError;
};
''',
)
insert_after(
    "src/config/main.cpp",
    '''RuntimePresentation runtime_presentation(
    const chatview::RuntimeTelemetrySnapshot &telemetry)
{
    if (!chatview::has_runtime_telemetry_flag(
            telemetry,
            chatview::RuntimeTelemetryHistoryValid)) {
        return {L"No restart history", kColorMuted};
    }

    const std::wstring reason =
        runtime_restart_reason_name(telemetry.restart_reason);
    const std::wstring exit = runtime_exit_suffix(telemetry);
    if (chatview::has_runtime_telemetry_flag(
            telemetry,
            chatview::RuntimeTelemetryCircuitOpen)) {
        return {
            L"Automatic restart blocked after " +
                std::to_wstring(telemetry.consecutive_failures) +
                L" failures — " + reason + exit,
            kColorError};
    }

    if (telemetry.consecutive_failures != 0U) {
        return {
            L"Automatic recovery pending — " + reason +
                L" (failure " +
                std::to_wstring(telemetry.consecutive_failures) +
                L"/" +
                std::to_wstring(
                    chatview::kMaximumRuntimeFailureCount) +
                L")" + exit,
            kColorWarning};
    }

    if (telemetry.restart_reason ==
        chatview::RuntimeRestartReason::ManualRestart) {
        return {L"Last restart requested manually", kColorMuted};
    }

    return {
        L"Last automatic recovery: " + reason +
            L" — failure counter cleared" + exit,
        kColorGood};
}
''',
    r'''
ReadinessPresentation readiness_presentation(
    const chatview::StreamReadinessResult &result)
{
    if (result.ready) {
        return {
            L"●  READY TO STREAM — Chat, private HUD, and capture safety are ready.",
            kColorGood};
    }

    using Blocker = chatview::StreamReadinessBlocker;
    switch (result.blocker) {
    case Blocker::ObsDisconnected:
        return {
            L"●  BLOCKED — Open this Control Center from the OBS Tools menu.",
            kColorError};
    case Blocker::StatusUnavailable:
        return {L"●  BLOCKED — OBS status is unavailable.", kColorError};
    case Blocker::ChatNotConfigured:
        return {
            L"●  BLOCKED — Save a supported chat URL.",
            kColorError};
    case Blocker::RestartCircuitOpen:
        return {
            L"●  BLOCKED — Automatic HUD restart is disabled after repeated failures.",
            kColorError};
    case Blocker::SceneGraphUnavailable:
        return {
            L"●  BLOCKED — OBS scene safety cannot be verified yet.",
            kColorError};
    case Blocker::DisplayCaptureActive:
        return {
            L"●  BLOCKED — Active Display Capture would hide the private HUD.",
            kColorError};
    case Blocker::HudNotRunning:
        return {L"●  BLOCKED — The private HUD is not running.", kColorError};
    case Blocker::HudHealthUnavailable:
        return {L"●  BLOCKED — HUD health cannot be verified.", kColorError};
    case Blocker::ChatStarting:
        return {L"●  BLOCKED — Chat engine is starting.", kColorWarning};
    case Blocker::ChatLoading:
        return {L"●  BLOCKED — Chat is still loading.", kColorWarning};
    case Blocker::ChatRetrying:
        return {L"●  BLOCKED — Chat navigation is retrying.", kColorWarning};
    case Blocker::ChatRecovering:
        return {L"●  BLOCKED — WebView is recovering.", kColorWarning};
    case Blocker::LoginRequired:
        return {L"●  BLOCKED — Sign in to the chat platform.", kColorError};
    case Blocker::BroadcastOffline:
        return {L"●  BLOCKED — The broadcast is offline or ended.", kColorError};
    case Blocker::LayoutChanged:
        return {L"●  BLOCKED — The platform page layout changed.", kColorError};
    case Blocker::NetworkOffline:
        return {L"●  BLOCKED — This PC is offline.", kColorError};
    case Blocker::ConnectionLost:
        return {L"●  BLOCKED — Chat connection is being restored.", kColorWarning};
    case Blocker::SystemPaused:
        return {L"●  BLOCKED — Windows session is paused.", kColorError};
    case Blocker::SystemResuming:
        return {L"●  BLOCKED — Windows session is being revalidated.", kColorWarning};
    case Blocker::ChatFatal:
        return {L"●  BLOCKED — Chat engine failed.", kColorError};
    case Blocker::HudHidden:
        return {L"●  BLOCKED — The private HUD window is hidden.", kColorError};
    case Blocker::None:
    default:
        return {L"●  BLOCKED — Stream readiness is unknown.", kColorError};
    }
}
''',
)
replace_once(
    "src/config/main.cpp",
    '''        subtitle_ = create_static(
            L"Configure chat and verify the private HUD before going live.");''',
    '''        subtitle_ = create_static(
            L"●  CHECKING STREAM READINESS");''',
)
replace_once(
    "src/config/main.cpp",
    '''            EnableWindow(edit_button_, FALSE);
            EnableWindow(restart_button_, FALSE);
            return;
        }

        const bool status_changed =''',
    '''            EnableWindow(edit_button_, FALSE);
            EnableWindow(restart_button_, FALSE);
            refresh_stream_readiness(false, false, {}, false, {});
            return;
        }

        const bool status_changed =''',
)
replace_once(
    "src/config/main.cpp",
    '''        if (!connected_ || !status_reader_.parent_alive()) {
            connected_ = false;
            snapshot_available_ = false;
            latest_health_available_ = false;
            set_disconnected_status();
            return;
        }
''',
    '''        if (!connected_ || !status_reader_.parent_alive()) {
            connected_ = false;
            snapshot_available_ = false;
            latest_health_available_ = false;
            set_disconnected_status();
            refresh_stream_readiness(false, false, {}, false, {});
            return;
        }
''',
)
replace_once(
    "src/config/main.cpp",
    '''        EnableWindow(
            edit_button_,
            hud_running && !capture_risk ? TRUE : FALSE);
        EnableWindow(restart_button_, TRUE);
    }

    void set_disconnected_status()
''',
    '''        EnableWindow(
            edit_button_,
            hud_running && !capture_risk ? TRUE : FALSE);
        EnableWindow(restart_button_, TRUE);
        refresh_stream_readiness(
            true, true, snapshot, health_available, health);
    }

    void refresh_stream_readiness(
        bool obs_connected,
        bool status_available,
        const chatview::ControlStatusSnapshot &status,
        bool health_available,
        const chatview::HudHealthSnapshot &health)
    {
        chatview::ChatConfig config;
        const bool chat_configured =
            chatview::load_chat_config(config);
        const chatview::StreamReadinessResult result =
            chatview::evaluate_stream_readiness({
                obs_connected,
                status_available,
                chat_configured,
                health_available,
                status,
                health});
        const ReadinessPresentation presentation =
            readiness_presentation(result);
        set_colored_text(
            subtitle_,
            presentation.text,
            presentation.color,
            readiness_color_);
    }

    void set_disconnected_status()
''',
)
replace_once(
    "src/config/main.cpp",
    '''        refresh_provider();
        set_feedback(
            connected_
                ? L"Saved and applied to the running HUD."
                : L"Saved. Open ChatView from OBS to apply it.",
            kColorGood);
''',
    '''        refresh_provider();
        refresh_runtime_status(true);
        set_feedback(
            connected_
                ? L"Saved and applied to the running HUD."
                : L"Saved. Open ChatView from OBS to apply it.",
            kColorGood);
''',
)
replace_once(
    "src/config/main.cpp",
    '''        if (control == subtitle_ || control == version_) {
            color = kColorMuted;
''',
    '''        if (control == subtitle_) {
            color = readiness_color_;
        } else if (control == version_) {
            color = kColorMuted;
''',
)
insert_after(
    "src/config/main.cpp",
    '''        for (HWND label : labels) {
            SendMessageW(
                label,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(label_font_),
                TRUE);
        }
''',
    '''        SendMessageW(
            subtitle_,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(label_font_),
            TRUE);
''',
)
replace_once(
    "src/config/main.cpp",
    '''    COLORREF provider_color_ = kColorMuted;
    COLORREF obs_color_ = kColorMuted;''',
    '''    COLORREF readiness_color_ = kColorWarning;
    COLORREF provider_color_ = kColorMuted;
    COLORREF obs_color_ = kColorMuted;''',
)

insert_after(
    "tests/config-single-instance-test.cpp",
    '''    mapped.get()->magic = chatview::kControlStatusMagic;
    mapped.get()->version = chatview::kControlStatusVersion;
''',
    '''    mapped.get()->last_hud_exit_code =
        chatview::kRuntimeExitCodeUnavailable;
''',
)
replace_once(
    "tests/config-single-instance-test.cpp",
    '''        chatview::ControlStatusHudRunning |
            chatview::ControlStatusHudVisible,
''',
    '''        chatview::ControlStatusHudRunning |
            chatview::ControlStatusHudVisible |
            chatview::ControlStatusSceneGraphReady,
''',
)
insert_after(
    "tests/config-single-instance-test.cpp",
    '''                       child_text_contains(
                           control_center, L"YouTube chat ready");
''',
    '''                       child_text_contains(
                           control_center,
                           L"BLOCKED — Save a supported chat URL");
''',
)
insert_after(
    "tests/config-single-instance-test.cpp",
    '''                       saved_url_matches(
                           config_file,
                           L"https://www.youtube.com/live_chat?is_popout=1&v=dQw4w9WgXcQ");
''',
    '''                       child_text_contains(
                           control_center, L"READY TO STREAM");
''',
)
insert_after(
    "tests/config-single-instance-test.cpp",
    '''            first.process.get());
    }

    if (!post_command(control_center, kEditButtonId, edit_button) ||
''',
    '''            first.process.get());
    }

    publish(
        mapped.get(),
        chatview::ControlStatusHudRunning |
            chatview::ControlStatusHudVisible |
            chatview::ControlStatusSceneGraphReady |
            chatview::ControlStatusDisplayCaptureActive,
        process_id,
        2U);
    SetEvent(status_event.get());
    if (!wait_until(
            [&]() {
                return child_text_contains(
                    control_center,
                    L"BLOCKED — Active Display Capture");
            },
            kWindowTimeoutMs)) {
        DestroyWindow(fake_hud);
        return fail(
            L"The readiness gate did not block active Display Capture",
            first.process.get());
    }

    publish(
        mapped.get(),
        chatview::ControlStatusHudRunning |
            chatview::ControlStatusHudVisible |
            chatview::ControlStatusSceneGraphReady,
        process_id,
        3U);
    SetEvent(status_event.get());
    if (!wait_until(
            [&]() {
                return child_text_contains(
                    control_center, L"READY TO STREAM");
            },
            kWindowTimeoutMs)) {
        DestroyWindow(fake_hud);
        return fail(
            L"The readiness gate did not recover after Display Capture cleared",
            first.process.get());
    }

    if (!post_command(control_center, kEditButtonId, edit_button) ||
''',
)

replace_once(
    "tests/control-status-reader-test.cpp",
    '''        chatview::ControlStatusCaptureRisk |
        chatview::ControlStatusHudRunning;''',
    '''        chatview::ControlStatusCaptureRisk |
        chatview::ControlStatusHudRunning |
        chatview::ControlStatusSceneGraphReady |
        chatview::ControlStatusDisplayCaptureActive;''',
)

insert_after(
    "README.md",
    '''- privacy-filtered diagnostics from OBS or the Control Center, with no automatic upload;
''',
    '''- a single **READY TO STREAM / BLOCKED** preflight verdict with the exact blocking condition;
''',
)
insert_after(
    "README.md",
    '''Use **Export diagnostics** in the Control Center, or **Tools → Export ChatView Diagnostics...** in OBS, to create a Desktop folder containing a binary-integrity summary, the current OBS/HUD state when available, and only ChatView-tagged OBS log lines. The export omits the configured URL and chat messages, redacts user-profile paths and IPC names, uploads nothing automatically, and must be reviewed before sharing.
''',
    '''
The Control Center's top line is a conservative preflight verdict. **READY TO STREAM** requires a saved supported chat URL, a stable OBS scene graph, no active Display Capture source, a running and visible HUD, a healthy platform page in the Ready state, and a closed automatic-restart circuit. **BLOCKED** names the first condition that must be fixed. The verdict is advisory and does not take control of OBS output buttons.
''',
)

insert_after(
    "docs/architecture.md",
    '''The plugin also runs a 100 ms output-safety monitor. It enumerates OBS input sources and recognizes the pinned Windows Display Capture source ID `monitor_capture`.''',
    ''' The same scan publishes scene-graph readiness and active Display Capture presence to the Control Center even while outputs are idle, allowing the preflight verdict to block before streaming starts.''',
)
