// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace chatview {

enum class PageHealthWatchdogAction : std::uint8_t {
    None = 0U,
    Reload = 1U,
    Restart = 2U,
};

class PageHealthWatchdog final {
public:
    static constexpr std::uint64_t kHeartbeatTimeoutMs = 12000U;
    static constexpr unsigned int kMaximumReloadAttempts = 2U;

    void arm(std::uint64_t now_ms) noexcept
    {
        armed_ = true;
        last_heartbeat_ms_ = now_ms;
        consecutive_timeouts_ = 0U;
    }

    void disarm() noexcept
    {
        armed_ = false;
        last_heartbeat_ms_ = 0U;
        consecutive_timeouts_ = 0U;
    }

    void heartbeat(std::uint64_t now_ms) noexcept
    {
        if (!armed_) {
            return;
        }
        last_heartbeat_ms_ = now_ms;
        consecutive_timeouts_ = 0U;
    }

    [[nodiscard]] PageHealthWatchdogAction poll(
        std::uint64_t now_ms) noexcept
    {
        if (!armed_) {
            return PageHealthWatchdogAction::None;
        }

        if (now_ms < last_heartbeat_ms_) {
            last_heartbeat_ms_ = now_ms;
            consecutive_timeouts_ = 0U;
            return PageHealthWatchdogAction::None;
        }

        if (now_ms - last_heartbeat_ms_ < kHeartbeatTimeoutMs) {
            return PageHealthWatchdogAction::None;
        }

        last_heartbeat_ms_ = now_ms;
        ++consecutive_timeouts_;
        if (consecutive_timeouts_ <= kMaximumReloadAttempts) {
            return PageHealthWatchdogAction::Reload;
        }

        armed_ = false;
        return PageHealthWatchdogAction::Restart;
    }

    [[nodiscard]] bool armed() const noexcept
    {
        return armed_;
    }

    [[nodiscard]] unsigned int consecutive_timeouts() const noexcept
    {
        return consecutive_timeouts_;
    }

private:
    std::uint64_t last_heartbeat_ms_ = 0U;
    unsigned int consecutive_timeouts_ = 0U;
    bool armed_ = false;
};

static_assert(PageHealthWatchdog::kHeartbeatTimeoutMs >= 6000U);
static_assert(PageHealthWatchdog::kMaximumReloadAttempts >= 1U);

} // namespace chatview
