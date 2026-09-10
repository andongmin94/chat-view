// SPDX-License-Identifier: GPL-2.0-or-later

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
