from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def replace_exact_count(
    path_text: str, old: str, new: str, expected: int
) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != expected:
        raise SystemExit(
            f"{path_text}: expected {expected} anchors, found {count}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.4.0 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.4.1 LANGUAGES CXX)",
)

replace_once(
    "src/common/stream-readiness.hpp",
    '''enum class StreamReadinessBlocker : std::uint8_t {
    None = 0U,
    ObsDisconnected,
    StatusUnavailable,
    ChatNotConfigured,
    RestartCircuitOpen,
    SceneGraphUnavailable,
    DisplayCaptureActive,
    HudNotRunning,
    HudHealthUnavailable,
    ChatStarting,
    ChatLoading,
    ChatRetrying,
    ChatRecovering,
    LoginRequired,
    BroadcastOffline,
    LayoutChanged,
    NetworkOffline,
    ConnectionLost,
    SystemPaused,
    SystemResuming,
    ChatFatal,
    HudHidden,
};

struct StreamReadinessInput {''',
    '''enum class StreamReadinessBlocker : std::uint8_t {
    None = 0U,
    ObsDisconnected,
    StatusUnavailable,
    ChatNotConfigured,
    RestartCircuitOpen,
    SceneGraphUnavailable,
    DisplayCaptureActive,
    HudNotRunning,
    HudHealthUnavailable,
    ChatStarting,
    ChatLoading,
    ChatRetrying,
    ChatRecovering,
    LoginRequired,
    BroadcastOffline,
    LayoutChanged,
    NetworkOffline,
    ConnectionLost,
    SystemPaused,
    SystemResuming,
    ChatFatal,
    HudHidden,
};

enum class StreamRecoveryAction : std::uint8_t {
    None = 0U,
    FocusChatUrl,
    RestartHud,
    OpenHudInteraction,
    ActivateObs,
    OpenNetworkSettings,
    ExportDiagnostics,
};

struct StreamReadinessInput {''',
)

replace_once(
    "src/common/stream-readiness.hpp",
    '''struct StreamReadinessResult {
    bool ready = false;
    StreamReadinessBlocker blocker =
        StreamReadinessBlocker::StatusUnavailable;
};

[[nodiscard]] constexpr StreamReadinessResult evaluate_stream_readiness(''',
    '''struct StreamReadinessResult {
    bool ready = false;
    StreamReadinessBlocker blocker =
        StreamReadinessBlocker::StatusUnavailable;
};

[[nodiscard]] constexpr StreamRecoveryAction recovery_action_for(
    StreamReadinessBlocker blocker) noexcept
{
    switch (blocker) {
    case StreamReadinessBlocker::ChatNotConfigured:
    case StreamReadinessBlocker::BroadcastOffline:
        return StreamRecoveryAction::FocusChatUrl;

    case StreamReadinessBlocker::RestartCircuitOpen:
    case StreamReadinessBlocker::HudNotRunning:
    case StreamReadinessBlocker::HudHealthUnavailable:
    case StreamReadinessBlocker::ChatRetrying:
    case StreamReadinessBlocker::ChatRecovering:
    case StreamReadinessBlocker::ConnectionLost:
    case StreamReadinessBlocker::ChatFatal:
        return StreamRecoveryAction::RestartHud;

    case StreamReadinessBlocker::LoginRequired:
    case StreamReadinessBlocker::HudHidden:
        return StreamRecoveryAction::OpenHudInteraction;

    case StreamReadinessBlocker::StatusUnavailable:
    case StreamReadinessBlocker::SceneGraphUnavailable:
    case StreamReadinessBlocker::DisplayCaptureActive:
        return StreamRecoveryAction::ActivateObs;

    case StreamReadinessBlocker::NetworkOffline:
        return StreamRecoveryAction::OpenNetworkSettings;

    case StreamReadinessBlocker::LayoutChanged:
        return StreamRecoveryAction::ExportDiagnostics;

    case StreamReadinessBlocker::None:
    case StreamReadinessBlocker::ObsDisconnected:
    case StreamReadinessBlocker::ChatStarting:
    case StreamReadinessBlocker::ChatLoading:
    case StreamReadinessBlocker::SystemPaused:
    case StreamReadinessBlocker::SystemResuming:
    default:
        return StreamRecoveryAction::None;
    }
}

[[nodiscard]] constexpr StreamReadinessResult evaluate_stream_readiness(''',
)

replace_once(
    "src/common/stream-readiness.hpp",
    '''static_assert(
    evaluate_stream_readiness(StreamReadinessInput{}).blocker ==
    StreamReadinessBlocker::ObsDisconnected);
''',
    '''static_assert(
    evaluate_stream_readiness(StreamReadinessInput{}).blocker ==
    StreamReadinessBlocker::ObsDisconnected);
static_assert(
    recovery_action_for(StreamReadinessBlocker::DisplayCaptureActive) ==
    StreamRecoveryAction::ActivateObs);
static_assert(
    recovery_action_for(StreamReadinessBlocker::ChatFatal) ==
    StreamRecoveryAction::RestartHud);
''',
)

replace_once(
    "tests/stream-readiness-test.cpp",
    '''    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::HudHidden)) {
        return fail("A hidden HUD was not blocked");
    }

    return 0;
}''',
    '''    if (!expect_blocker(
            input,
            chatview::StreamReadinessBlocker::HudHidden)) {
        return fail("A hidden HUD was not blocked");
    }

    struct RecoveryCase {
        chatview::StreamReadinessBlocker blocker;
        chatview::StreamRecoveryAction action;
    };
    constexpr std::array<RecoveryCase, 22U> recovery_cases{{
        {chatview::StreamReadinessBlocker::None,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::ObsDisconnected,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::StatusUnavailable,
         chatview::StreamRecoveryAction::ActivateObs},
        {chatview::StreamReadinessBlocker::ChatNotConfigured,
         chatview::StreamRecoveryAction::FocusChatUrl},
        {chatview::StreamReadinessBlocker::RestartCircuitOpen,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::SceneGraphUnavailable,
         chatview::StreamRecoveryAction::ActivateObs},
        {chatview::StreamReadinessBlocker::DisplayCaptureActive,
         chatview::StreamRecoveryAction::ActivateObs},
        {chatview::StreamReadinessBlocker::HudNotRunning,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::HudHealthUnavailable,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::ChatStarting,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::ChatLoading,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::ChatRetrying,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::ChatRecovering,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::LoginRequired,
         chatview::StreamRecoveryAction::OpenHudInteraction},
        {chatview::StreamReadinessBlocker::BroadcastOffline,
         chatview::StreamRecoveryAction::FocusChatUrl},
        {chatview::StreamReadinessBlocker::LayoutChanged,
         chatview::StreamRecoveryAction::ExportDiagnostics},
        {chatview::StreamReadinessBlocker::NetworkOffline,
         chatview::StreamRecoveryAction::OpenNetworkSettings},
        {chatview::StreamReadinessBlocker::ConnectionLost,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::SystemPaused,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::SystemResuming,
         chatview::StreamRecoveryAction::None},
        {chatview::StreamReadinessBlocker::ChatFatal,
         chatview::StreamRecoveryAction::RestartHud},
        {chatview::StreamReadinessBlocker::HudHidden,
         chatview::StreamRecoveryAction::OpenHudInteraction},
    }};
    for (const RecoveryCase &recovery_case : recovery_cases) {
        if (chatview::recovery_action_for(recovery_case.blocker) !=
            recovery_case.action) {
            return fail("A readiness blocker selected the wrong recovery action");
        }
    }

    return 0;
}''',
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    '''constexpr DWORD kWindowStateTimeoutMs = 3000U;
constexpr DWORD kCaptureSettleTimeMs = 180U;''',
    '''constexpr DWORD kWindowStateTimeoutMs = 3000U;
constexpr DWORD kControlMessageTimeoutMs = 5000U;
constexpr DWORD kCaptureSettleTimeMs = 180U;''',
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    '''bool wait_for_style(HWND window, LONG_PTR required, LONG_PTR forbidden) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kWindowStateTimeoutMs;
    while (GetTickCount64() < deadline) {
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if ((style & required) == required && (style & forbidden) == 0) {
            return true;
        }
        Sleep(25U);
    }
    return false;
}

bool wait_for_visibility(''',
    '''bool wait_for_style(HWND window, LONG_PTR required, LONG_PTR forbidden) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kWindowStateTimeoutMs;
    while (GetTickCount64() < deadline) {
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if ((style & required) == required && (style & forbidden) == 0) {
            return true;
        }
        Sleep(25U);
    }
    return false;
}

bool send_control_message(HWND window, UINT message) noexcept
{
    DWORD_PTR ignored = 0U;
    return SendMessageTimeoutW(
               window,
               message,
               0U,
               0L,
               SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT,
               kControlMessageTimeoutMs,
               &ignored) != FALSE;
}

bool wait_for_visibility(''',
)

replace_exact_count(
    "src/preflight/hud-self-test.cpp",
    "!PostMessageW(window, toggle_edit_message, 0U, 0L)",
    "!send_control_message(window, toggle_edit_message)",
    4,
)

replace_once(
    "src/config/main.cpp",
    '''constexpr DWORD kHudHealthQueryTimeoutMs = 40U;
constexpr int kUrlEditId = 1001;''',
    '''constexpr DWORD kHudHealthQueryTimeoutMs = 40U;
constexpr DWORD kHudControlTimeoutMs = 1500U;
constexpr int kUrlEditId = 1001;''',
)
replace_once(
    "src/config/main.cpp",
    '''constexpr int kDiagnosticsButtonId = 1006;
constexpr int kWindowWidthDip = 960;
constexpr int kWindowHeightDip = 570;''',
    '''constexpr int kDiagnosticsButtonId = 1006;
constexpr int kRecoveryButtonId = 1007;
constexpr int kWindowWidthDip = 960;
constexpr int kWindowHeightDip = 620;''',
)

replace_once(
    "src/config/main.cpp",
    '''BOOL CALLBACK find_hud_window(HWND window, LPARAM data)''',
    '''const wchar_t *recovery_action_label(
    chatview::StreamRecoveryAction action) noexcept
{
    switch (action) {
    case chatview::StreamRecoveryAction::FocusChatUrl:
        return L"Enter chat URL";
    case chatview::StreamRecoveryAction::RestartHud:
        return L"Restart HUD";
    case chatview::StreamRecoveryAction::OpenHudInteraction:
        return L"Open private HUD";
    case chatview::StreamRecoveryAction::ActivateObs:
        return L"Open OBS";
    case chatview::StreamRecoveryAction::OpenNetworkSettings:
        return L"Open network settings";
    case chatview::StreamRecoveryAction::ExportDiagnostics:
        return L"Export diagnostics";
    case chatview::StreamRecoveryAction::None:
    default:
        return L"";
    }
}

BOOL CALLBACK find_hud_window(HWND window, LPARAM data)''',
)

replace_once(
    "src/config/main.cpp",
    '''    return TRUE;
}

HWND hud_window_for_process(DWORD process_id) noexcept''',
    '''    return TRUE;
}

BOOL CALLBACK find_application_window(HWND window, LPARAM data)
{
    auto *search = static_cast<WindowSearch *>(
        reinterpret_cast<void *>(data));
    if (search == nullptr || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }

    DWORD process_id = 0U;
    GetWindowThreadProcessId(window, &process_id);
    const LONG_PTR extended_style =
        GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (process_id == search->process_id &&
        (extended_style & WS_EX_TOOLWINDOW) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HWND application_window_for_process(DWORD process_id) noexcept
{
    if (process_id == 0U) {
        return nullptr;
    }

    WindowSearch search{process_id, nullptr};
    EnumWindows(
        &find_application_window,
        reinterpret_cast<LPARAM>(&search));
    return search.window;
}

HWND hud_window_for_process(DWORD process_id) noexcept''',
)

replace_once(
    "src/config/main.cpp",
    '''    health = decoded;
    return true;
}

std::wstring output_description(''',
    '''    health = decoded;
    return true;
}

bool send_hud_control_message(HWND hud, UINT message) noexcept
{
    if (hud == nullptr || message == 0U) {
        return false;
    }

    DWORD_PTR ignored = 0U;
    return SendMessageTimeoutW(
               hud,
               message,
               0U,
               0L,
               SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT,
               kHudControlTimeoutMs,
               &ignored) != FALSE;
}

std::wstring output_description(''',
)

replace_once(
    "src/config/main.cpp",
    '''            case kDiagnosticsButtonId:
                export_diagnostics();
                return 0L;
            case kCloseButtonId:''',
    '''            case kDiagnosticsButtonId:
                export_diagnostics();
                return 0L;
            case kRecoveryButtonId:
                perform_recovery_action();
                return 0L;
            case kCloseButtonId:''',
)

replace_once(
    "src/config/main.cpp",
    '''        version_ = create_static(
            L"ChatView " CHATVIEW_WIDEN(CHATVIEW_VERSION));

        save_button_ = create_button(''',
    '''        version_ = create_static(
            L"ChatView " CHATVIEW_WIDEN(CHATVIEW_VERSION));

        recovery_button_ = create_button(
            L"Fix now", kRecoveryButtonId, BS_DEFPUSHBUTTON);
        save_button_ = create_button(''',
)

replace_once(
    "src/config/main.cpp",
    '''        const std::array<HWND, 21U> required{
            title_,
            subtitle_,''',
    '''        const std::array<HWND, 22U> required{
            title_,
            subtitle_,''',
)
replace_once(
    "src/config/main.cpp",
    '''            recovery_value_,
            feedback_,
            save_button_,''',
    '''            recovery_value_,
            feedback_,
            recovery_button_,
            save_button_,''',
)

replace_once(
    "src/config/main.cpp",
    '''        set_colored_text(
            subtitle_,
            presentation.text,
            presentation.color,
            readiness_color_);
    }

    void set_disconnected_status()''',
    '''        set_colored_text(
            subtitle_,
            presentation.text,
            presentation.color,
            readiness_color_);
        set_recovery_action(
            chatview::recovery_action_for(result.blocker));
    }

    void set_recovery_action(chatview::StreamRecoveryAction action)
    {
        recovery_action_ = action;
        if (recovery_button_ == nullptr) {
            return;
        }

        if (action == chatview::StreamRecoveryAction::None) {
            EnableWindow(recovery_button_, FALSE);
            ShowWindow(recovery_button_, SW_HIDE);
            return;
        }

        SetWindowTextW(
            recovery_button_, recovery_action_label(action));
        EnableWindow(recovery_button_, TRUE);
        ShowWindow(recovery_button_, SW_SHOWNOACTIVATE);
    }

    void perform_recovery_action()
    {
        switch (recovery_action_) {
        case chatview::StreamRecoveryAction::FocusChatUrl:
            focus_chat_url();
            return;
        case chatview::StreamRecoveryAction::RestartHud:
            restart_hud();
            return;
        case chatview::StreamRecoveryAction::OpenHudInteraction:
            open_hud_interaction();
            return;
        case chatview::StreamRecoveryAction::ActivateObs:
            activate_obs();
            return;
        case chatview::StreamRecoveryAction::OpenNetworkSettings:
            open_network_settings();
            return;
        case chatview::StreamRecoveryAction::ExportDiagnostics:
            export_diagnostics();
            return;
        case chatview::StreamRecoveryAction::None:
        default:
            return;
        }
    }

    void focus_chat_url()
    {
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
        SetFocus(url_edit_);
        SendMessageW(url_edit_, EM_SETSEL, 0U, -1L);
        set_feedback(
            L"Paste the current broadcast or chat URL, then choose Save & Apply.",
            kColorWarning);
    }

    void activate_obs()
    {
        const HWND obs = application_window_for_process(
            options_.parent_process_id);
        if (obs == nullptr) {
            set_feedback(
                L"The OBS window could not be brought forward.",
                kColorError);
            return;
        }

        ShowWindow(obs, SW_RESTORE);
        SetForegroundWindow(obs);
        set_feedback(
            L"OBS was brought forward. Resolve the scene or Display Capture warning.",
            kColorWarning);
    }

    void open_hud_interaction()
    {
        if (!connected_ || !status_reader_.parent_alive()) {
            set_feedback(L"OBS is not connected.", kColorError);
            return;
        }

        chatview::ControlStatusSnapshot snapshot;
        if (!status_reader_.read(snapshot) ||
            !chatview::has_control_status_flag(
                snapshot, chatview::ControlStatusHudRunning) ||
            chatview::has_control_status_flag(
                snapshot, chatview::ControlStatusCaptureRisk) ||
            chatview::has_control_status_flag(
                snapshot,
                chatview::ControlStatusDisplayCaptureActive)) {
            set_feedback(
                L"The private HUD cannot be opened while capture safety is active.",
                kColorWarning);
            return;
        }

        const HWND hud = hud_window_for_process(snapshot.hud_process_id);
        if (hud == nullptr) {
            set_feedback(
                L"The private HUD window could not be found.",
                kColorError);
            return;
        }

        const LONG_PTR extended_style =
            GetWindowLongPtrW(hud, GWL_EXSTYLE);
        const bool interactive =
            (extended_style &
             static_cast<LONG_PTR>(
                 WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) == 0;
        if (!interactive) {
            const UINT message = RegisterWindowMessageW(
                chatview::kToggleEditMessageName);
            if (!send_hud_control_message(hud, message)) {
                set_feedback(
                    L"ChatView could not open the private HUD for interaction.",
                    kColorError);
                return;
            }
        }

        ShowWindow(hud, SW_SHOW);
        SetForegroundWindow(hud);
        set_feedback(
            L"Private HUD interaction opened. Sign in or reposition it, then lock it again.",
            kColorGood);
    }

    void open_network_settings()
    {
        const HINSTANCE opened = ShellExecuteW(
            window_,
            L"open",
            L"ms-settings:network-status",
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(opened) <= 32) {
            set_feedback(
                L"Windows network settings could not be opened.",
                kColorError);
            return;
        }
        set_feedback(
            L"Windows network settings opened.", kColorGood);
    }

    void set_disconnected_status()''',
)

replace_once(
    "src/config/main.cpp",
    '''        if (hud == nullptr || message == 0U ||
            !PostMessageW(hud, message, 0U, 0L)) {''',
    '''        if (!send_hud_control_message(hud, message)) {''',
)

replace_once(
    "src/config/main.cpp",
    '''        const std::array<HWND, 22U> body_controls{
            subtitle_,''',
    '''        const std::array<HWND, 23U> body_controls{
            subtitle_,''',
)
replace_once(
    "src/config/main.cpp",
    '''            feedback_,
            save_button_,''',
    '''            feedback_,
            recovery_button_,
            save_button_,''',
)

replace_once(
    "src/config/main.cpp",
    '''        move(title_, 32, 24, 896, 38);
        move(subtitle_, 34, 61, 892, 24);
        move(url_label_, 34, 105, 300, 22);
        move(url_edit_, 34, 132, 892, 32);
        move(provider_value_, 36, 170, 888, 24);
        move(status_group_, 28, 207, 904, 226);
        move(obs_label_, 52, 239, 190, 24);
        move(obs_value_, 248, 239, 650, 24);
        move(hud_label_, 52, 275, 190, 24);
        move(hud_value_, 248, 275, 650, 24);
        move(safety_label_, 52, 311, 190, 24);
        move(safety_value_, 248, 311, 650, 24);
        move(output_label_, 52, 347, 190, 24);
        move(output_value_, 248, 347, 650, 24);
        move(recovery_label_, 52, 383, 190, 24);
        move(recovery_value_, 248, 383, 650, 24);
        move(feedback_, 34, 443, 570, 24);
        move(save_button_, 34, 479, 142, 38);
        move(edit_button_, 186, 479, 142, 38);
        move(restart_button_, 338, 479, 142, 38);
        move(diagnostics_button_, 490, 479, 198, 38);
        move(close_button_, 806, 479, 120, 38);
        move(version_, 680, 445, 246, 22);''',
    '''        move(title_, 32, 24, 896, 38);
        move(subtitle_, 34, 61, 892, 24);
        move(recovery_button_, 34, 92, 240, 38);
        move(url_label_, 34, 145, 300, 22);
        move(url_edit_, 34, 172, 892, 32);
        move(provider_value_, 36, 210, 888, 24);
        move(status_group_, 28, 247, 904, 226);
        move(obs_label_, 52, 279, 190, 24);
        move(obs_value_, 248, 279, 650, 24);
        move(hud_label_, 52, 315, 190, 24);
        move(hud_value_, 248, 315, 650, 24);
        move(safety_label_, 52, 351, 190, 24);
        move(safety_value_, 248, 351, 650, 24);
        move(output_label_, 52, 387, 190, 24);
        move(output_value_, 248, 387, 650, 24);
        move(recovery_label_, 52, 423, 190, 24);
        move(recovery_value_, 248, 423, 650, 24);
        move(feedback_, 34, 483, 570, 24);
        move(save_button_, 34, 529, 142, 38);
        move(edit_button_, 186, 529, 142, 38);
        move(restart_button_, 338, 529, 142, 38);
        move(diagnostics_button_, 490, 529, 198, 38);
        move(close_button_, 806, 529, 120, 38);
        move(version_, 680, 485, 246, 22);''',
)

replace_once(
    "src/config/main.cpp",
    '''    HWND feedback_ = nullptr;
    HWND version_ = nullptr;
    HWND save_button_ = nullptr;''',
    '''    HWND feedback_ = nullptr;
    HWND version_ = nullptr;
    HWND recovery_button_ = nullptr;
    HWND save_button_ = nullptr;''',
)
replace_once(
    "src/config/main.cpp",
    '''    bool latest_health_available_ = false;
    COLORREF readiness_color_ = kColorWarning;''',
    '''    bool latest_health_available_ = false;
    chatview::StreamRecoveryAction recovery_action_ =
        chatview::StreamRecoveryAction::None;
    COLORREF readiness_color_ = kColorWarning;''',
)

replace_once(
    "tests/config-single-instance-test.cpp",
    '''constexpr int kRestartButtonId = 1004;
constexpr DWORD kWindowTimeoutMs = 8000U;''',
    '''constexpr int kRestartButtonId = 1004;
constexpr int kRecoveryButtonId = 1007;
constexpr DWORD kWindowTimeoutMs = 8000U;''',
)
replace_once(
    "tests/config-single-instance-test.cpp",
    '''                       child_text_contains(
                           control_center,
                           L"BLOCKED — Save a supported chat URL");''',
    '''                       child_text_contains(
                           control_center,
                           L"BLOCKED — Save a supported chat URL") &&
                       child_text_contains(
                           control_center, L"Enter chat URL");''',
)
replace_once(
    "tests/config-single-instance-test.cpp",
    '''    const HWND restart_button = GetDlgItem(control_center, kRestartButtonId);
    if (url_edit == nullptr || save_button == nullptr ||
        edit_button == nullptr || restart_button == nullptr) {''',
    '''    const HWND restart_button = GetDlgItem(control_center, kRestartButtonId);
    const HWND recovery_button = GetDlgItem(control_center, kRecoveryButtonId);
    if (url_edit == nullptr || save_button == nullptr ||
        edit_button == nullptr || restart_button == nullptr ||
        recovery_button == nullptr || !IsWindowVisible(recovery_button)) {''',
)
replace_once(
    "tests/config-single-instance-test.cpp",
    '''                       child_text_contains(
                           control_center, L"READY TO STREAM");''',
    '''                       child_text_contains(
                           control_center, L"READY TO STREAM") &&
                       !IsWindowVisible(recovery_button);''',
)
replace_once(
    "tests/config-single-instance-test.cpp",
    '''                return child_text_contains(
                    control_center,
                    L"BLOCKED — Active Display Capture");''',
    '''                return child_text_contains(
                           control_center,
                           L"BLOCKED — Active Display Capture") &&
                       child_text_contains(
                           control_center, L"Open OBS") &&
                       IsWindowVisible(recovery_button);''',
)
replace_once(
    "tests/config-single-instance-test.cpp",
    '''                return child_text_contains(
                    control_center, L"READY TO STREAM");''',
    '''                return child_text_contains(
                           control_center, L"READY TO STREAM") &&
                       !IsWindowVisible(recovery_button);''',
)

replace_once(
    "README.md",
    '''- privacy-filtered diagnostics from OBS or the Control Center, with no automatic upload;
- repeated live HUD/WebView2 resource-growth soak coverage;''',
    '''- privacy-filtered diagnostics from OBS or the Control Center, with no automatic upload;
- a conservative **READY TO STREAM / BLOCKED** verdict with one blocker-specific recovery action;
- repeated live HUD/WebView2 resource-growth soak coverage;''',
)
replace_once(
    "README.md",
    '''Use **Export diagnostics** in the Control Center, or **Tools → Export ChatView Diagnostics...** in OBS, to create a Desktop folder containing a binary-integrity summary, the current OBS/HUD state when available, the persisted last HUD exit and restart-circuit history, and only ChatView-tagged OBS log lines. The export omits the configured URL and chat messages, redacts user-profile paths and IPC names, uploads nothing automatically, and must be reviewed before sharing.
''',
    '''Use **Export diagnostics** in the Control Center, or **Tools → Export ChatView Diagnostics...** in OBS, to create a Desktop folder containing a binary-integrity summary, the current OBS/HUD state when available, the persisted last HUD exit and restart-circuit history, and only ChatView-tagged OBS log lines. The export omits the configured URL and chat messages, redacts user-profile paths and IPC names, uploads nothing automatically, and must be reviewed before sharing.

The Control Center preflight line stays fail-closed: it shows **READY TO STREAM** only after chat health, HUD visibility, scene readiness, and Display Capture safety are all verified. A blocked verdict exposes exactly one context-sensitive action, such as entering a chat URL, opening OBS, restarting the HUD, opening the private HUD for login, opening Windows network settings, or exporting diagnostics. It does not automatically change OBS scenes or sources.
''',
)
