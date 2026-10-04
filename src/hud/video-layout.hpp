// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>

namespace chatview {
// First supported pixel path is bounded SDR BGRA, not an HDR conversion path.
constexpr int kVideoMaxDimension = 4096;
struct VideoRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const VideoRect &) const = default;
};
constexpr bool video_size(int width, int height) noexcept
{
    return width > 0 && height > 0 && width <= kVideoMaxDimension && height <= kVideoMaxDimension;
}
// The pool retains its per-axis high-water capacity until capture stops. Shrink
// and a return to an already seen size must not discard queued frames through
// Recreate. A late smaller surface is still never safe to draw.
enum class VideoFrameAction { Invalid, Present, PresentAndResize, ResizeOnly, WaitForSurface };
constexpr VideoFrameAction video_frame_action(int width, int height, unsigned surface_width,
                                              unsigned surface_height, int pool_width, int pool_height) noexcept
{
    if (!video_size(width, height) || !video_size(pool_width, pool_height) ||
        surface_width == 0 || surface_height == 0 || surface_width > kVideoMaxDimension || surface_height > kVideoMaxDimension)
        return VideoFrameAction::Invalid;
    const bool grow = width > pool_width || height > pool_height;
    if (surface_width < static_cast<unsigned>(width) || surface_height < static_cast<unsigned>(height))
        return grow ? VideoFrameAction::ResizeOnly : VideoFrameAction::WaitForSurface;
    return grow ? VideoFrameAction::PresentAndResize : VideoFrameAction::Present;
}
constexpr bool video_overlap(VideoRect a, VideoRect b) noexcept
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}
constexpr VideoRect video_fit(int source_width, int source_height, int width, int height) noexcept
{
    if (!video_size(source_width, source_height) || !video_size(width, height)) return {};
    const auto sw = static_cast<std::int64_t>(source_width), sh = static_cast<std::int64_t>(source_height);
    int w = width, h = height;
    if (static_cast<std::int64_t>(width) * sh > static_cast<std::int64_t>(height) * sw)
        w = std::max(1, static_cast<int>(height * sw / sh));
    else h = std::max(1, static_cast<int>(width * sh / sw));
    const int x = (width - w) / 2, y = (height - h) / 2;
    return {x, y, x + w, y + h};
}
// No clone/same-screen output. Source/HUD spanning the output is rejected too.
constexpr bool separate_video_output(VideoRect output, VideoRect source, VideoRect hud,
                                      bool extended, bool primary) noexcept
{
    return extended && !primary && output.right > output.left && output.bottom > output.top &&
        !video_overlap(output, source) && !video_overlap(output, hud);
}
}
