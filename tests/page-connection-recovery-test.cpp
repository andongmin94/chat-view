// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/page-connection-recovery.hpp"

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
    using chatview::PageConnectionRecovery;
    using chatview::PageConnectionRecoveryAction;

    PageConnectionRecovery recovery;
    if (recovery.active() || recovery.reload_requested() ||
        recovery.poll(50000U) != PageConnectionRecoveryAction::None) {
        return fail("A new connection recovery policy was unexpectedly active");
    }

    recovery.begin(1000U);
    recovery.begin(5000U);
    if (!recovery.active() || recovery.reload_requested() ||
        recovery.poll(10999U) != PageConnectionRecoveryAction::None ||
        recovery.poll(11000U) != PageConnectionRecoveryAction::Reload ||
        !recovery.reload_requested()) {
        return fail("A persistent disconnect did not request one delayed reload");
    }

    if (recovery.poll(25999U) != PageConnectionRecoveryAction::None ||
        recovery.poll(26000U) != PageConnectionRecoveryAction::Restart ||
        recovery.active()) {
        return fail("A disconnect that survived reload did not request restart");
    }

    recovery.begin(40000U);
    recovery.reset();
    if (recovery.active() || recovery.reload_requested() ||
        recovery.poll(999999U) != PageConnectionRecoveryAction::None) {
        return fail("Reset left a stale connection recovery deadline");
    }

    recovery.begin(100000U);
    if (recovery.poll(90000U) != PageConnectionRecoveryAction::None ||
        recovery.poll(99999U) != PageConnectionRecoveryAction::None ||
        recovery.poll(100000U) != PageConnectionRecoveryAction::Reload) {
        return fail("Clock regression handling produced the wrong recovery deadline");
    }

    recovery.reset();
    recovery.begin(0U);
    if (recovery.poll(PageConnectionRecovery::kReloadDelayMs) !=
            PageConnectionRecoveryAction::Reload ||
        recovery.poll(
            PageConnectionRecovery::kReloadDelayMs +
            PageConnectionRecovery::kRestartDelayMs) !=
            PageConnectionRecoveryAction::Restart) {
        return fail("Recovery delay constants did not match their exact boundaries");
    }

    return 0;
}
