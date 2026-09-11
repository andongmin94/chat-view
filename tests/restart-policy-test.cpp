// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/restart-policy.hpp"

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
    chatview::RestartPolicy policy;
    if (!policy.automatic_restart_allowed() ||
        policy.failure_count() != 0U ||
        policy.delay_ms() != 0U) {
        return fail("A new restart policy was not immediately usable");
    }

    constexpr std::array<std::uint32_t, 5U> expected_delays{
        500U,
        1000U,
        2000U,
        4000U,
        8000U,
    };

    for (std::size_t index = 0U; index < expected_delays.size(); ++index) {
        policy.record_failure();
        if (!policy.automatic_restart_allowed() ||
            policy.failure_count() != index + 1U ||
            policy.delay_ms() != expected_delays[index]) {
            return fail("Automatic restart backoff changed unexpectedly");
        }
    }

    policy.record_failure();
    if (policy.automatic_restart_allowed() ||
        policy.failure_count() !=
            chatview::kMaximumAutomaticRestartFailures ||
        policy.delay_ms() != 0U) {
        return fail("The automatic restart circuit did not open at its limit");
    }

    policy.record_failure();
    if (policy.failure_count() !=
        chatview::kMaximumAutomaticRestartFailures) {
        return fail("The restart failure counter did not saturate");
    }

    policy.reset();
    policy.restore(3U);
    if (!policy.automatic_restart_allowed() ||
        policy.failure_count() != 3U || policy.delay_ms() != 2000U) {
        return fail("A persisted restart count was not restored");
    }

    policy.restore(999U);
    if (policy.automatic_restart_allowed() ||
        policy.failure_count() !=
            chatview::kMaximumAutomaticRestartFailures) {
        return fail("A restored restart count did not saturate safely");
    }

    policy.reset();
    if (!policy.automatic_restart_allowed() ||
        policy.failure_count() != 0U ||
        policy.delay_ms() != 0U) {
        return fail("An explicit reset did not close the restart circuit");
    }

    return 0;
}
