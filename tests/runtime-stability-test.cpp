// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/restart-policy.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

int main()
{
    using chatview::runtime_stability_wait_ms;
    constexpr std::uint64_t ready_at = 500U;
    // State events every 100 ms must consume the same interval, not restart it.
    for (std::uint32_t age = 0U; age < 30000U; age += 100U) {
        if (runtime_stability_wait_ms(ready_at, ready_at + age) !=
            30000U - age) {
            std::cerr << "A publish event restarted the stability interval\n";
            return 1;
        }
    }
    if (runtime_stability_wait_ms(ready_at, ready_at + 29999U) != 1U ||
        runtime_stability_wait_ms(ready_at, ready_at + 30000U) != 0U ||
        runtime_stability_wait_ms(ready_at, ready_at + 90000U) != 0U) {
        std::cerr << "The stability boundary is incorrect\n";
        return 1;
    }
    // A replacement HUD gets its own ready timestamp.
    if (runtime_stability_wait_ms(100000U, 100001U) != 29999U) {
        std::cerr << "A replacement inherited an old stability deadline\n";
        return 1;
    }
    constexpr std::uint64_t near_wrap =
        std::numeric_limits<std::uint64_t>::max() - 999U;
    if (runtime_stability_wait_ms(near_wrap, near_wrap + 30000U) != 0U) {
        std::cerr << "Unsigned monotonic age calculation failed\n";
        return 1;
    }
    // The timer must not modify the failure counter or reopen a circuit.
    chatview::RestartPolicy policy;
    policy.restore(chatview::kMaximumAutomaticRestartFailures);
    (void)runtime_stability_wait_ms(0U, 30000U);
    if (policy.automatic_restart_allowed() || policy.failure_count() != 6U) {
        std::cerr << "The timer changed crash-loop protection\n";
        return 1;
    }
    return 0;
}
