// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/runtime-telemetry.hpp"

#include <array>
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
    if (!chatview::is_valid_runtime_telemetry({})) {
        return fail("The empty runtime telemetry snapshot was rejected");
    }

    const chatview::RuntimeTelemetrySnapshot automatic{
        19U,
        chatview::RuntimeRestartReason::PageHealthTimeout,
        2U,
        chatview::RuntimeTelemetryHistoryValid |
            chatview::RuntimeTelemetryAutomatic,
        133485408000000000ULL,
    };
    if (!chatview::is_valid_runtime_telemetry(automatic)) {
        return fail("A valid automatic restart snapshot was rejected");
    }

    chatview::RuntimeTelemetrySnapshot circuit = automatic;
    circuit.consecutive_failures = chatview::kMaximumRuntimeFailureCount;
    circuit.flags |= chatview::RuntimeTelemetryCircuitOpen;
    if (!chatview::is_valid_runtime_telemetry(circuit)) {
        return fail("A valid open restart circuit was rejected");
    }

    chatview::RuntimeTelemetrySnapshot manual{
        chatview::kRuntimeExitCodeUnavailable,
        chatview::RuntimeRestartReason::ManualRestart,
        0U,
        chatview::RuntimeTelemetryHistoryValid,
        133485408000000001ULL,
    };
    if (!chatview::is_valid_runtime_telemetry(manual)) {
        return fail("A valid manual restart snapshot was rejected");
    }

    chatview::RuntimeTelemetrySnapshot invalid = automatic;
    invalid.flags |= 1U << 31U;
    if (chatview::is_valid_runtime_telemetry(invalid)) {
        return fail("Unknown telemetry flags were accepted");
    }

    invalid = automatic;
    invalid.flags |= chatview::RuntimeTelemetryCircuitOpen;
    if (chatview::is_valid_runtime_telemetry(invalid)) {
        return fail("A restart circuit opened below the failure limit");
    }

    invalid = manual;
    invalid.flags |= chatview::RuntimeTelemetryAutomatic;
    if (chatview::is_valid_runtime_telemetry(invalid)) {
        return fail("A manual restart was marked automatic");
    }

    invalid = automatic;
    invalid.event_filetime_utc = 0U;
    if (chatview::is_valid_runtime_telemetry(invalid)) {
        return fail("Telemetry without an event timestamp was accepted");
    }

    constexpr std::array<std::pair<std::uint32_t,
                                   chatview::RuntimeRestartReason>,
                         8U>
        known_exit_codes{{
            {2U, chatview::RuntimeRestartReason::WebViewFailure},
            {10U, chatview::RuntimeRestartReason::NavigationFailure},
            {11U, chatview::RuntimeRestartReason::CaptureExclusionFailure},
            {13U, chatview::RuntimeRestartReason::ReadySignalFailure},
            {18U, chatview::RuntimeRestartReason::PlacementFailure},
            {19U, chatview::RuntimeRestartReason::PageHealthTimeout},
            {20U, chatview::RuntimeRestartReason::ConnectionRecovery},
            {21U, chatview::RuntimeRestartReason::SystemLifecycleRecovery},
        }};
    for (const auto &[exit_code, reason] : known_exit_codes) {
        if (chatview::runtime_restart_reason_from_exit_code(exit_code) !=
            reason) {
            return fail("A known HUD exit code mapped to the wrong reason");
        }
    }

    if (chatview::runtime_restart_reason_from_exit_code(777U) !=
        chatview::RuntimeRestartReason::UnexpectedExit) {
        return fail("An unknown HUD exit code was not classified safely");
    }

    return 0;
}
