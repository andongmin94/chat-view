// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/page-health-watchdog.hpp"

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
    using chatview::PageHealthWatchdog;
    using chatview::PageHealthWatchdogAction;

    PageHealthWatchdog watchdog;
    if (watchdog.armed() ||
        watchdog.poll(50000U) != PageHealthWatchdogAction::None) {
        return fail("A new watchdog was unexpectedly armed");
    }

    watchdog.arm(1000U);
    if (!watchdog.armed() || watchdog.consecutive_timeouts() != 0U) {
        return fail("Arming the watchdog did not reset its state");
    }
    if (watchdog.poll(12999U) != PageHealthWatchdogAction::None ||
        watchdog.poll(13000U) != PageHealthWatchdogAction::Reload ||
        watchdog.consecutive_timeouts() != 1U) {
        return fail("The first heartbeat timeout was not detected at the boundary");
    }
    if (watchdog.poll(24999U) != PageHealthWatchdogAction::None ||
        watchdog.poll(25000U) != PageHealthWatchdogAction::Reload ||
        watchdog.consecutive_timeouts() != 2U) {
        return fail("The second heartbeat timeout did not request another reload");
    }
    if (watchdog.poll(37000U) != PageHealthWatchdogAction::Restart ||
        watchdog.armed() || watchdog.consecutive_timeouts() != 3U) {
        return fail("Repeated stale heartbeats did not escalate to a full restart");
    }

    watchdog.heartbeat(40000U);
    if (watchdog.armed()) {
        return fail("A heartbeat armed a watchdog that had already stopped");
    }

    watchdog.arm(50000U);
    if (watchdog.poll(62000U) != PageHealthWatchdogAction::Reload) {
        return fail("The rearmed watchdog did not detect a timeout");
    }
    watchdog.heartbeat(62500U);
    if (watchdog.consecutive_timeouts() != 0U ||
        watchdog.poll(74499U) != PageHealthWatchdogAction::None ||
        watchdog.poll(74500U) != PageHealthWatchdogAction::Reload) {
        return fail("A valid heartbeat did not reset the escalation counter");
    }

    watchdog.arm(100000U);
    if (watchdog.poll(90000U) != PageHealthWatchdogAction::None ||
        watchdog.consecutive_timeouts() != 0U ||
        watchdog.poll(101999U) != PageHealthWatchdogAction::None ||
        watchdog.poll(102000U) != PageHealthWatchdogAction::Reload) {
        return fail("Clock regression handling produced an early timeout");
    }

    watchdog.disarm();
    if (watchdog.armed() || watchdog.consecutive_timeouts() != 0U ||
        watchdog.poll(999999U) != PageHealthWatchdogAction::None) {
        return fail("Disarming the watchdog left stale state behind");
    }

    return 0;
}
