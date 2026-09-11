// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace chatview {

enum class SystemLifecycleAction : std::uint8_t {
    None = 0U,
    Pause = 1U,
    Resume = 2U,
};

class SystemLifecycle final {
public:
    [[nodiscard]] SystemLifecycleAction suspend() noexcept
    {
        return update(power_suspended_, true);
    }

    [[nodiscard]] SystemLifecycleAction resume() noexcept
    {
        return update(power_suspended_, false);
    }

    [[nodiscard]] SystemLifecycleAction lock_session() noexcept
    {
        return update(session_locked_, true);
    }

    [[nodiscard]] SystemLifecycleAction unlock_session() noexcept
    {
        return update(session_locked_, false);
    }

    [[nodiscard]] SystemLifecycleAction begin_end_session() noexcept
    {
        return update(end_session_pending_, true);
    }

    [[nodiscard]] SystemLifecycleAction cancel_end_session() noexcept
    {
        return update(end_session_pending_, false);
    }

    void reset() noexcept
    {
        power_suspended_ = false;
        session_locked_ = false;
        end_session_pending_ = false;
    }

    [[nodiscard]] bool paused() const noexcept
    {
        return power_suspended_ || session_locked_ || end_session_pending_;
    }

    [[nodiscard]] bool power_suspended() const noexcept
    {
        return power_suspended_;
    }

    [[nodiscard]] bool session_locked() const noexcept
    {
        return session_locked_;
    }

    [[nodiscard]] bool end_session_pending() const noexcept
    {
        return end_session_pending_;
    }

private:
    [[nodiscard]] SystemLifecycleAction update(
        bool &flag,
        bool value) noexcept
    {
        const bool was_paused = paused();
        flag = value;
        const bool is_paused = paused();
        if (!was_paused && is_paused) {
            return SystemLifecycleAction::Pause;
        }
        if (was_paused && !is_paused) {
            return SystemLifecycleAction::Resume;
        }
        return SystemLifecycleAction::None;
    }

    bool power_suspended_ = false;
    bool session_locked_ = false;
    bool end_session_pending_ = false;
};

} // namespace chatview
