from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "src/hud/hud-window.hpp",
    """    void complete_system_resume() noexcept;
    [[nodiscard]] bool system_suppressed() const noexcept;
    void handle_webview_process_failure(
""",
    """    void complete_system_resume() noexcept;
    [[nodiscard]] bool system_suppressed() const noexcept;
    [[nodiscard]] bool signal_runtime_ready() noexcept;
    void handle_webview_process_failure(
""",
)

replace_once(
    "src/hud/hud-window.cpp",
    """        if (ready_event_ != nullptr &&
            !SetEvent(ready_event_)) {
            debug_windows_error(L"SetEvent(ready)");
            set_page_health(
                HudPageState::Fatal,
                page_provider_,
                static_cast<std::uint16_t>(13U));
            ShowWindow(window_, SW_HIDE);
            PostQuitMessage(13);
        }
        return 0L;
""",
    """        if (!signal_runtime_ready()) {
            return 0L;
        }
        return 0L;
""",
)

replace_once(
    "src/hud/hud-window.cpp",
    """    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();

    if (edit_mode_) {
""",
    """    page_connection_recovery_.reset();
    page_health_watchdog_.disarm();

    if (!webview_ready_) {
        restart_after_system_resume_ = true;
        if (!signal_runtime_ready()) {
            return;
        }
    }

    if (edit_mode_) {
""",
)

replace_once(
    "src/hud/hud-window.cpp",
    """bool HudWindow::system_suppressed() const noexcept
{
    return system_lifecycle_.paused() || system_resume_pending_;
}

bool HudWindow::check_page_connection_recovery() noexcept
""",
    """bool HudWindow::system_suppressed() const noexcept
{
    return system_lifecycle_.paused() || system_resume_pending_;
}

bool HudWindow::signal_runtime_ready() noexcept
{
    if (ready_event_ == nullptr) {
        return true;
    }

    const HANDLE ready_event = ready_event_;
    ready_event_ = nullptr;
    if (SetEvent(ready_event)) {
        return true;
    }

    debug_windows_error(L"SetEvent(ready)");
    set_page_health(
        HudPageState::Fatal,
        page_provider_,
        static_cast<std::uint16_t>(13U));
    ShowWindow(window_, SW_HIDE);
    PostQuitMessage(13);
    return false;
}

bool HudWindow::check_page_connection_recovery() noexcept
""",
)
