// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace chatview {

enum class PageConnectionRecoveryAction : std::uint8_t {
    None = 0U,
    Reload = 1U,
    Restart = 2U,
};

class PageConnectionRecovery final {
public:
    static constexpr std::uint64_t kReloadDelayMs = 10000U;
    static constexpr std::uint64_t kRestartDelayMs = 15000U;

    void begin(std::uint64_t now_ms) noexcept
    {
        if (active_) {
            return;
        }

        active_ = true;
        reload_requested_ = false;
        stage_started_ms_ = now_ms;
    }

    void reset() noexcept
    {
        active_ = false;
        reload_requested_ = false;
        stage_started_ms_ = 0U;
    }

    [[nodiscard]] PageConnectionRecoveryAction poll(
        std::uint64_t now_ms) noexcept
    {
        if (!active_) {
            return PageConnectionRecoveryAction::None;
        }

        if (now_ms < stage_started_ms_) {
            stage_started_ms_ = now_ms;
            return PageConnectionRecoveryAction::None;
        }

        const std::uint64_t delay =
            reload_requested_ ? kRestartDelayMs : kReloadDelayMs;
        if (now_ms - stage_started_ms_ < delay) {
            return PageConnectionRecoveryAction::None;
        }

        stage_started_ms_ = now_ms;
        if (!reload_requested_) {
            reload_requested_ = true;
            return PageConnectionRecoveryAction::Reload;
        }

        active_ = false;
        return PageConnectionRecoveryAction::Restart;
    }

    [[nodiscard]] bool active() const noexcept
    {
        return active_;
    }

    [[nodiscard]] bool reload_requested() const noexcept
    {
        return reload_requested_;
    }

private:
    std::uint64_t stage_started_ms_ = 0U;
    bool active_ = false;
    bool reload_requested_ = false;
};

static_assert(PageConnectionRecovery::kReloadDelayMs >= 5000U);
static_assert(PageConnectionRecovery::kRestartDelayMs >= 10000U);

} // namespace chatview
