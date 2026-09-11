from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "src/hud/hud-window.cpp",
    """        update_host_state();
        if (!capture_exclusion_intact()) {
            fail_closed_capture_exclusion();
            return 0L;
        }
        if (!apply_capture_policy()) {
""",
    """        update_host_state();
        if (!system_suppressed() &&
            !capture_exclusion_intact()) {
            fail_closed_capture_exclusion();
            return 0L;
        }
        if (!apply_capture_policy()) {
""",
)

replace_once(
    "src/hud/hud-window.cpp",
    """        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMECRITICAL:
        case PBT_APMRESUMESUSPEND:
            handle_system_lifecycle_action(
                system_lifecycle_.resume(),
                kPowerResumedDetail);
            break;
""",
    """        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
            handle_system_lifecycle_action(
                system_lifecycle_.resume(),
                kPowerResumedDetail);
            break;
        case PBT_APMRESUMECRITICAL: {
            SystemLifecycleAction action = system_lifecycle_.resume();
            if (action == SystemLifecycleAction::None &&
                !system_lifecycle_.paused() &&
                !system_resume_pending_) {
                action = SystemLifecycleAction::Resume;
            }
            handle_system_lifecycle_action(
                action, kPowerResumedDetail);
            break;
        }
""",
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    """    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Windows resume did not restore capture exclusion",
            child_process.get());
    }

    SendMessageW(
        window, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0L);
""",
    """    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Windows resume did not restore capture exclusion",
            child_process.get());
    }

    if (SendMessageW(
            window,
            WM_POWERBROADCAST,
            PBT_APMRESUMECRITICAL,
            0L) != TRUE) {
        return fail(
            L"The HUD rejected a critical resume without prior suspend",
            child_process.get());
    }
    Sleep(200U);
    if (IsWindowVisible(window)) {
        return fail(
            L"The HUD skipped critical-resume revalidation",
            child_process.get());
    }
    if (!wait_for_visibility(window, true)) {
        return fail(
            L"The HUD did not return after critical-resume revalidation",
            child_process.get());
    }
    affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(window, &affinity) ||
        affinity != WDA_EXCLUDEFROMCAPTURE) {
        return fail(
            L"Critical resume did not retain capture exclusion",
            child_process.get());
    }

    SendMessageW(
        window, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0L);
""",
)
