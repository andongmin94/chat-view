// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

namespace chatview {

// OBS Studio 32.2.2 uses this unversioned ID for Windows Display Capture.
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
