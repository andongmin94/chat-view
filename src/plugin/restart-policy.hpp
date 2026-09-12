// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/runtime-telemetry.hpp"

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kMaximumAutomaticRestartFailures =
    kMaximumRuntimeFailureCount;
inline constexpr std::uint32_t kInitialAutomaticRestartDelayMs = 500U;
inline constexpr std::uint32_t kMaximumAutomaticRestartDelayMs = 30000U;

inline constexpr std::uint32_t kRuntimeStablePeriodMs = 30000U;

// Use runtime age, not a fresh interval after each state-publish wakeup.
[[nodiscard]] constexpr std::uint32_t runtime_stability_wait_ms(
    std::uint64_t ready_at_ms, std::uint64_t now_ms) noexcept
{
    const std::uint64_t elapsed = now_ms - ready_at_ms;
    return elapsed >= kRuntimeStablePeriodMs
               ? 0U
               : kRuntimeStablePeriodMs - static_cast<std::uint32_t>(elapsed);
}

class RestartPolicy final {
public:
    constexpr void record_failure() noexcept
    {
        if (failure_count_ < kMaximumAutomaticRestartFailures) {
            ++failure_count_;
        }
    }

    constexpr void restore(std::uint32_t failure_count) noexcept
    {
        failure_count_ =
            failure_count > kMaximumAutomaticRestartFailures
                ? kMaximumAutomaticRestartFailures
                : failure_count;
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
