// SPDX-License-Identifier: GPL-2.0-or-later

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

    struct RecoveryCase {
        chatview::StreamReadinessBlocker blocker;
        chatview::StreamRecoveryAction action;
    };
    constexpr std::array<RecoveryCase, 22U> recovery_cases{{
        {chatview::StreamReadinessBlocker::None,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::ObsDisconnected,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::StatusUnavailable,
         chatview::StreamRecoveryAction::ActivateObs},
        {chatview::StreamReadinessBlocker::ChatNotConfigured,
         chatview::StreamRecoveryAction::FocusChatUrl},
        {chatview::StreamReadinessBlocker::RestartCircuitOpen,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::SceneGraphUnavailable,
         chatview::StreamRecoveryAction::ActivateObs},
        {chatview::StreamReadinessBlocker::DisplayCaptureActive,
         chatview::StreamRecoveryAction::ActivateObs},
        {chatview::StreamReadinessBlocker::HudNotRunning,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::HudHealthUnavailable,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::ChatStarting,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::ChatLoading,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::ChatRetrying,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::ChatRecovering,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::LoginRequired,
         chatview::StreamRecoveryAction::OpenHudInteraction},
        {chatview::StreamReadinessBlocker::BroadcastOffline,
         chatview::StreamRecoveryAction::FocusChatUrl},
        {chatview::StreamReadinessBlocker::LayoutChanged,
         chatview::StreamRecoveryAction::ExportDiagnostics},
        {chatview::StreamReadinessBlocker::NetworkOffline,
         chatview::StreamRecoveryAction::OpenNetworkSettings},
        {chatview::StreamReadinessBlocker::ConnectionLost,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::SystemPaused,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::SystemResuming,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::ChatFatal,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::HudHidden,
         chatview::StreamRecoveryAction::OpenHudInteraction},
    }};
    for (const RecoveryCase &recovery_case : recovery_cases) {
        if (chatview::recovery_action_for(recovery_case.blocker) !=
            recovery_case.action) {
            return fail("A readiness blocker selected the wrong recovery action");
        }
    }

    return 0;
}
