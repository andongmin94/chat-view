// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>
#include <cstdint>

namespace chatview {
inline constexpr std::uint64_t kVideoFrameLifetimeMs = 2000;

// Both the capture worker and the HWND owner enforce the same content age.
// Zero means no valid frame. Never extend a timestamp after a slow GPU call.
constexpr bool video_frame_fresh(std::uint64_t now, std::uint64_t content_at) noexcept
{
    return content_at != 0 && now >= content_at && now - content_at < kVideoFrameLifetimeMs;
}
inline std::uint64_t video_frame_time(std::uint64_t now, double age_seconds) noexcept
{
    if (!std::isfinite(age_seconds) || age_seconds < -0.1 ||
        age_seconds >= static_cast<double>(kVideoFrameLifetimeMs) / 1000.0) return 0;
    // Round age up so millisecond conversion never rejuvenates old content.
    const auto age_ms = static_cast<std::uint64_t>(std::ceil(age_seconds > 0 ? age_seconds * 1000.0 : 0.0));
    return now > age_ms ? now - age_ms : 0;
}
}
