// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

namespace chatview {

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

[[nodiscard]] constexpr bool should_suppress_private_hud(
    bool display_capture_active,
    bool streaming,
    bool recording,
    bool replay_buffer,
    bool virtual_camera) noexcept
{
    return display_capture_active &&
           (streaming || recording || replay_buffer || virtual_camera);
}

static_assert(is_capture_risk_source_id("monitor_capture"));
static_assert(!is_capture_risk_source_id("window_capture"));
static_assert(should_suppress_private_hud(
    true, true, false, false, false));
static_assert(!should_suppress_private_hud(
    true, false, false, false, false));

} // namespace chatview
