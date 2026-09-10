// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kMaximumAutomaticRestartFailures = 6U;
inline constexpr std::uint32_t kInitialAutomaticRestartDelayMs = 500U;
inline constexpr std::uint32_t kMaximumAutomaticRestartDelayMs = 30000U;

class RestartPolicy final {
public:
    constexpr void record_failure() noexcept
    {
        if (failure_count_ < kMaximumAutomaticRestartFailures) {
            ++failure_count_;
        }
    }

    constexpr void reset() noexcept
    {
        failure_count_ = 0U;
    }

    [[nodiscard]] constexpr bool automatic_restart_allowed() const noexcept
    {
        return failure_count_ < kMaximumAutomaticRestartFailures;
    }

    [[nodiscard]] constexpr std::uint32_t failure_count() const noexcept
    {
        return failure_count_;
    }

    [[nodiscard]] constexpr std::uint32_t delay_ms() const noexcept
    {
        if (failure_count_ == 0U || !automatic_restart_allowed()) {
            return 0U;
        }

        std::uint32_t delay = kInitialAutomaticRestartDelayMs;
        for (std::uint32_t index = 1U; index < failure_count_; ++index) {
            if (delay >= kMaximumAutomaticRestartDelayMs / 2U) {
                return kMaximumAutomaticRestartDelayMs;
            }
            delay *= 2U;
        }
        return delay > kMaximumAutomaticRestartDelayMs
                   ? kMaximumAutomaticRestartDelayMs
                   : delay;
    }

private:
    std::uint32_t failure_count_ = 0U;
};

static_assert(RestartPolicy{}.automatic_restart_allowed());
static_assert(RestartPolicy{}.failure_count() == 0U);
static_assert(RestartPolicy{}.delay_ms() == 0U);

} // namespace chatview
