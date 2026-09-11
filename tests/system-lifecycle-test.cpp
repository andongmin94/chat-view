// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/system-lifecycle.hpp"

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
    using chatview::SystemLifecycle;
    using chatview::SystemLifecycleAction;

    SystemLifecycle lifecycle;
    if (lifecycle.paused() || lifecycle.power_suspended() ||
        lifecycle.session_locked()) {
        return fail("A new lifecycle state was unexpectedly paused");
    }

    if (lifecycle.resume() != SystemLifecycleAction::None ||
        lifecycle.unlock_session() != SystemLifecycleAction::None) {
        return fail("A duplicate active notification caused a transition");
    }

    if (lifecycle.suspend() != SystemLifecycleAction::Pause ||
        !lifecycle.paused() || !lifecycle.power_suspended() ||
        lifecycle.suspend() != SystemLifecycleAction::None) {
        return fail("Power suspend was not idempotent");
    }
    if (lifecycle.resume() != SystemLifecycleAction::Resume ||
        lifecycle.paused()) {
        return fail("Power resume did not return to the active state");
    }

    if (lifecycle.lock_session() != SystemLifecycleAction::Pause ||
        lifecycle.suspend() != SystemLifecycleAction::None ||
        lifecycle.unlock_session() != SystemLifecycleAction::None ||
        !lifecycle.paused() || !lifecycle.power_suspended() ||
        lifecycle.session_locked()) {
        return fail("Overlapping session and power pauses were not combined");
    }
    if (lifecycle.resume() != SystemLifecycleAction::Resume ||
        lifecycle.paused()) {
        return fail("The final pause reason did not trigger one resume");
    }

    if (lifecycle.lock_session() != SystemLifecycleAction::Pause ||
        lifecycle.unlock_session() != SystemLifecycleAction::Resume ||
        lifecycle.unlock_session() != SystemLifecycleAction::None) {
        return fail("Session lock transitions were not idempotent");
    }

    if (lifecycle.begin_end_session() != SystemLifecycleAction::Pause ||
        !lifecycle.end_session_pending() ||
        lifecycle.begin_end_session() != SystemLifecycleAction::None ||
        lifecycle.cancel_end_session() != SystemLifecycleAction::Resume ||
        lifecycle.end_session_pending()) {
        return fail("Cancelled shutdown transitions were not idempotent");
    }

    if (lifecycle.lock_session() != SystemLifecycleAction::Pause ||
        lifecycle.begin_end_session() != SystemLifecycleAction::None ||
        lifecycle.unlock_session() != SystemLifecycleAction::None ||
        !lifecycle.paused() || !lifecycle.end_session_pending() ||
        lifecycle.cancel_end_session() != SystemLifecycleAction::Resume ||
        lifecycle.paused()) {
        return fail("Shutdown and session-lock pause reasons were not combined");
    }

    lifecycle.suspend();
    lifecycle.lock_session();
    lifecycle.begin_end_session();
    lifecycle.reset();
    if (lifecycle.paused() || lifecycle.power_suspended() ||
        lifecycle.session_locked() || lifecycle.end_session_pending()) {
        return fail("Reset left a stale Windows pause reason");
    }

    return 0;
}
