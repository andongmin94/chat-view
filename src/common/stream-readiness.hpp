// SPDX-License-Identifier: GPL-2.0-or-later

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

enum class StreamRecoveryAction : std::uint8_t {
    None = 0U,
    FocusChatUrl,
    RestartHud,
    OpenHudInteraction,
    ActivateObs,
    OpenNetworkSettings,
    ExportDiagnostics,
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

[[nodiscard]] constexpr StreamRecoveryAction recovery_action_for(
    StreamReadinessBlocker blocker) noexcept
{
    switch (blocker) {
    case StreamReadinessBlocker::ChatNotConfigured:
    case StreamReadinessBlocker::BroadcastOffline:
        return StreamRecoveryAction::FocusChatUrl;

    case StreamReadinessBlocker::RestartCircuitOpen:
    case StreamReadinessBlocker::HudNotRunning:
    case StreamReadinessBlocker::HudHealthUnavailable:
    case StreamReadinessBlocker::ChatRetrying:
    case StreamReadinessBlocker::ChatRecovering:
    case StreamReadinessBlocker::ConnectionLost:
    case StreamReadinessBlocker::ChatFatal:
        return StreamRecoveryAction::RestartHud;

    case StreamReadinessBlocker::LoginRequired:
    case StreamReadinessBlocker::HudHidden:
        return StreamRecoveryAction::OpenHudInteraction;

    case StreamReadinessBlocker::StatusUnavailable:
    case StreamReadinessBlocker::SceneGraphUnavailable:
    case StreamReadinessBlocker::DisplayCaptureActive:
        return StreamRecoveryAction::ActivateObs;

    case StreamReadinessBlocker::NetworkOffline:
        return StreamRecoveryAction::OpenNetworkSettings;

    case StreamReadinessBlocker::LayoutChanged:
        return StreamRecoveryAction::ExportDiagnostics;

    case StreamReadinessBlocker::None:
    case StreamReadinessBlocker::ObsDisconnected:
    case StreamReadinessBlocker::ChatStarting:
    case StreamReadinessBlocker::ChatLoading:
    case StreamReadinessBlocker::SystemPaused:
    case StreamReadinessBlocker::SystemResuming:
    default:
        return StreamRecoveryAction::None;
    }
}

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
static_assert(
    recovery_action_for(StreamReadinessBlocker::DisplayCaptureActive) ==
    StreamRecoveryAction::ActivateObs);
static_assert(
    recovery_action_for(StreamReadinessBlocker::ChatFatal) ==
    StreamRecoveryAction::RestartHud);

} // namespace chatview
