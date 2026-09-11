from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "src/hud/system-lifecycle.hpp",
    """    [[nodiscard]] SystemLifecycleAction unlock_session() noexcept
    {
        return update(session_locked_, false);
    }

    void reset() noexcept
""",
    """    [[nodiscard]] SystemLifecycleAction unlock_session() noexcept
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
""",
)
replace_once(
    "src/hud/system-lifecycle.hpp",
    """        power_suspended_ = false;
        session_locked_ = false;
    }

    [[nodiscard]] bool paused() const noexcept
    {
        return power_suspended_ || session_locked_;
""",
    """        power_suspended_ = false;
        session_locked_ = false;
        end_session_pending_ = false;
    }

    [[nodiscard]] bool paused() const noexcept
    {
        return power_suspended_ || session_locked_ || end_session_pending_;
""",
)
replace_once(
    "src/hud/system-lifecycle.hpp",
    """    [[nodiscard]] bool session_locked() const noexcept
    {
        return session_locked_;
    }

private:
""",
    """    [[nodiscard]] bool session_locked() const noexcept
    {
        return session_locked_;
    }

    [[nodiscard]] bool end_session_pending() const noexcept
    {
        return end_session_pending_;
    }

private:
""",
)
replace_once(
    "src/hud/system-lifecycle.hpp",
    """    bool power_suspended_ = false;
    bool session_locked_ = false;
};
""",
    """    bool power_suspended_ = false;
    bool session_locked_ = false;
    bool end_session_pending_ = false;
};
""",
)

replace_once(
    "tests/system-lifecycle-test.cpp",
    """    if (lifecycle.lock_session() != SystemLifecycleAction::Pause ||
        lifecycle.unlock_session() != SystemLifecycleAction::Resume ||
        lifecycle.unlock_session() != SystemLifecycleAction::None) {
        return fail("Session lock transitions were not idempotent");
    }

    lifecycle.suspend();
    lifecycle.lock_session();
    lifecycle.reset();
    if (lifecycle.paused() || lifecycle.power_suspended() ||
        lifecycle.session_locked()) {
        return fail("Reset left a stale Windows pause reason");
    }
""",
    """    if (lifecycle.lock_session() != SystemLifecycleAction::Pause ||
        lifecycle.unlock_session() != SystemLifecycleAction::Resume ||
        lifecycle.unlock_session() != SystemLifecycleAction::None) {
        return fail("Session lock transitions were not idempotent");
    }

    if (lifecycle.begin_end_session() != SystemLifecycleAction::Pause ||
        !lifecycle.end_session_pending() ||
        lifecycle.begin_end_session() != SystemLifecycleAction::None ||
        lifecycle.cancel_end_session() != SystemLifecycleAction::Resume ||
        lifecycle.end_session_pending()) {
        return fail("Cancelled shutdown transitions were not idempotent");
    }

    if (lifecycle.lock_session() != SystemLifecycleAction::Pause ||
        lifecycle.begin_end_session() != SystemLifecycleAction::None ||
        lifecycle.unlock_session() != SystemLifecycleAction::None ||
        !lifecycle.paused() || !lifecycle.end_session_pending() ||
        lifecycle.cancel_end_session() != SystemLifecycleAction::Resume ||
        lifecycle.paused()) {
        return fail("Shutdown and session-lock pause reasons were not combined");
    }

    lifecycle.suspend();
    lifecycle.lock_session();
    lifecycle.begin_end_session();
    lifecycle.reset();
    if (lifecycle.paused() || lifecycle.power_suspended() ||
        lifecycle.session_locked() || lifecycle.end_session_pending()) {
        return fail("Reset left a stale Windows pause reason");
    }
""",
)

replace_once(
    "src/hud/hud-window.cpp",
    """constexpr std::uint16_t kSessionUnlockedDetail =
    kSystemLifecycleDetailBase + 4U;
constexpr UINT kReadyDurationMs = 2200U;
""",
    """constexpr std::uint16_t kSessionUnlockedDetail =
    kSystemLifecycleDetailBase + 4U;
constexpr std::uint16_t kSessionEndingDetail =
    kSystemLifecycleDetailBase + 5U;
constexpr std::uint16_t kSessionEndCancelledDetail =
    kSystemLifecycleDetailBase + 6U;
constexpr UINT kReadyDurationMs = 2200U;
""",
)
replace_once(
    "src/hud/hud-window.cpp",
    """    case WM_QUERYENDSESSION:
        ShowWindow(window_, SW_HIDE);
        return TRUE;
    case WM_ENDSESSION:
        if (wparam != FALSE) {
            page_connection_recovery_.reset();
            page_health_watchdog_.disarm();
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(0);
        }
        return 0L;
""",
    """    case WM_QUERYENDSESSION:
        handle_system_lifecycle_action(
            system_lifecycle_.begin_end_session(),
            kSessionEndingDetail);
        return TRUE;
    case WM_ENDSESSION:
        if (wparam != FALSE) {
            KillTimer(window_, kSystemResumeTimerId);
            page_connection_recovery_.reset();
            page_health_watchdog_.disarm();
            system_lifecycle_.reset();
            system_resume_pending_ = false;
            restart_after_system_resume_ = false;
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(0);
        } else {
            handle_system_lifecycle_action(
                system_lifecycle_.cancel_end_session(),
                kSessionEndCancelledDetail);
        }
        return 0L;
""",
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    """    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Session unlock did not restore capture exclusion",
            child_process.get());
    }

    const std::filesystem::path placement_file =
""",
    """    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Session unlock did not restore capture exclusion",
            child_process.get());
    }

    if (SendMessageW(
            window, WM_QUERYENDSESSION, 0U, 0L) != TRUE ||
        !wait_for_visibility(window, false)) {
        return fail(
            L"The HUD did not hide for the Windows end-session query",
            child_process.get());
    }
    SendMessageW(window, WM_ENDSESSION, FALSE, 0L);
    Sleep(200U);
    if (IsWindowVisible(window)) {
        return fail(
            L"The HUD became visible before cancelled-shutdown revalidation",
            child_process.get());
    }
    if (!wait_for_visibility(window, true)) {
        return fail(
            L"The HUD did not return after shutdown was cancelled",
            child_process.get());
    }
    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Cancelled shutdown did not restore capture exclusion",
            child_process.get());
    }

    const std::filesystem::path placement_file =
""",
)

replace_once(
    "README.md",
    """The platform page probe sends a compact health heartbeat every two seconds, even when the detected state has not changed. It also reports browser offline/online transitions and provider reconnecting or disconnected banners from bounded structural status regions rather than ordinary chat messages. The native HUD watchdog reloads a configured chat page after twelve seconds without a valid heartbeat. A separately bounded connection-recovery policy gives a reported disconnect ten seconds to recover, reloads the page once, and restarts the HUD if the disconnect survives another fifteen seconds. Browser-offline time and Display Capture suppression time do not count toward either recovery deadline.

`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint.
""",
    """The platform page probe sends a compact health heartbeat every two seconds, even when the detected state has not changed. It also reports browser offline/online transitions and provider reconnecting or disconnected banners from bounded structural status regions rather than ordinary chat messages. The native HUD watchdog reloads a configured chat page after twelve seconds without a valid heartbeat. A separately bounded connection-recovery policy gives a reported disconnect ten seconds to recover, reloads the page once, and restarts the HUD if the disconnect survives another fifteen seconds. Browser-offline time and Display Capture suppression time do not count toward either recovery deadline.

Windows suspend, workstation lock, and pending logoff or shutdown immediately hide the HUD, leave edit mode, and pause page recovery. The HUD remains hidden until every pause reason has cleared. After resume, unlock, or a cancelled shutdown it waits briefly for DWM and networking to settle, reapplies and verifies capture exclusion, restores monitor-relative bounds, and reloads the configured chat page. A WebView failure received while Windows is paused is deferred and converted into one clean HUD-process restart after the session returns, avoiding restart loops behind the lock screen.

`WDA_EXCLUDEFROMCAPTURE` is a best-effort Windows capture hint.
""",
)
