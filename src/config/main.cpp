// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/chat-config.hpp"
#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"
#include "config/control-status-reader.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace {

#define CHATVIEW_WIDEN_INNER(value) L##value
#define CHATVIEW_WIDEN(value) CHATVIEW_WIDEN_INNER(value)

constexpr wchar_t kWindowClassName[] = L"ChatViewObsConfigWindow";
constexpr UINT_PTR kRefreshTimerId = 1U;
constexpr UINT kRefreshIntervalMs = 250U;
constexpr int kUrlEditId = 1001;
constexpr int kSaveButtonId = 1002;
constexpr int kEditButtonId = 1003;
constexpr int kRestartButtonId = 1004;
constexpr int kCloseButtonId = 1005;
constexpr int kWindowWidthDip = 820;
constexpr int kWindowHeightDip = 520;
constexpr int kMinimumUrlLength = 0;
constexpr int kMaximumUrlLength = 2048;

constexpr COLORREF kColorText = RGB(32, 33, 36);
constexpr COLORREF kColorMuted = RGB(95, 99, 104);
constexpr COLORREF kColorGood = RGB(24, 128, 56);
constexpr COLORREF kColorWarning = RGB(184, 109, 0);
constexpr COLORREF kColorError = RGB(176, 0, 32);

struct Options {
    std::wstring status_mapping_name;
    std::wstring status_event_name;
    std::wstring restart_event_name;
    DWORD parent_process_id = 0U;

    [[nodiscard]] bool connected() const noexcept
    {
        return !status_mapping_name.empty() &&
               !status_event_name.empty() &&
               !restart_event_name.empty() &&
               parent_process_id != 0U;
    }
};

struct WindowSearch {
    DWORD process_id = 0U;
    HWND window = nullptr;
};

void enable_per_monitor_dpi_awareness() noexcept
{
    using SetProcessDpiAwarenessContextFunction =
        BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);

    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto set_awareness =
        reinterpret_cast<SetProcessDpiAwarenessContextFunction>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (set_awareness != nullptr) {
        set_awareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

UINT system_dpi() noexcept
{
    using GetDpiForSystemFunction = UINT(WINAPI *)();
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto get_dpi = reinterpret_cast<GetDpiForSystemFunction>(
        GetProcAddress(user32, "GetDpiForSystem"));
    const UINT dpi = get_dpi != nullptr ? get_dpi() : 96U;
    return dpi == 0U ? 96U : dpi;
}

int scale_dip(int value, UINT dpi) noexcept
{
    return MulDiv(value, static_cast<int>(dpi), 96);
}

bool parse_parent_process_id(
    const wchar_t *text, DWORD &process_id) noexcept
{
    errno = 0;
    wchar_t *end = nullptr;
    const unsigned long value = std::wcstoul(text, &end, 10);
    if (errno == ERANGE || end == nullptr || end == text ||
        *end != L'\0' || value == 0UL ||
        value > static_cast<unsigned long>(MAXDWORD)) {
        return false;
    }

    process_id = static_cast<DWORD>(value);
    return true;
}

bool parse_options(Options &options) noexcept
{
    int argument_count = 0;
    wchar_t **arguments = CommandLineToArgvW(
        GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        return false;
    }

    bool valid = true;
    for (int index = 1; index < argument_count && valid; ++index) {
        const std::wstring argument = arguments[index];
        if (argument == L"--status-mapping" &&
            index + 1 < argument_count) {
            options.status_mapping_name = arguments[++index];
        } else if (argument == L"--status-event" &&
                   index + 1 < argument_count) {
            options.status_event_name = arguments[++index];
        } else if (argument == L"--restart-event" &&
                   index + 1 < argument_count) {
            options.restart_event_name = arguments[++index];
        } else if (argument == L"--parent" &&
                   index + 1 < argument_count) {
            valid = parse_parent_process_id(
                arguments[++index], options.parent_process_id);
        } else {
            valid = false;
        }
    }

    LocalFree(arguments);
    const bool any_connection_argument =
        !options.status_mapping_name.empty() ||
        !options.status_event_name.empty() ||
        !options.restart_event_name.empty() ||
        options.parent_process_id != 0U;
    return valid && (!any_connection_argument || options.connected());
}

std::wstring trim(std::wstring value)
{
    const auto first = std::find_if_not(
        value.begin(), value.end(), [](wchar_t character) {
            return std::iswspace(character) != 0;
        });
    const auto last = std::find_if_not(
                          value.rbegin(),
                          value.rend(),
                          [](wchar_t character) {
                              return std::iswspace(character) != 0;
                          })
                          .base();

    if (first >= last) {
        return {};
    }
    return std::wstring(first, last);
}

std::wstring control_text(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }

    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(control, value.data(), length + 1);
    if (copied <= 0) {
        return {};
    }
    value.resize(static_cast<std::size_t>(copied));
    return value;
}

std::wstring provider_name(const std::wstring &normalized_url)
{
    if (normalized_url.starts_with(L"https://weflab.com/")) {
        return L"Weflab";
    }
    if (normalized_url.starts_with(L"https://chzzk.naver.com/")) {
        return L"CHZZK";
    }
    if (normalized_url.starts_with(L"https://play.sooplive.com/")) {
        return L"SOOP";
    }
    if (normalized_url.starts_with(L"https://www.youtube.com/")) {
        return L"YouTube";
    }
    return L"Unknown";
}

BOOL CALLBACK find_hud_window(HWND window, LPARAM data)
{
    auto *search = reinterpret_cast<WindowSearch *>(data);
    if (search == nullptr) {
        return FALSE;
    }

    DWORD process_id = 0U;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != search->process_id) {
        return TRUE;
    }

    std::array<wchar_t, 64U> class_name{};
    const int length = GetClassNameW(
        window,
        class_name.data(),
        static_cast<int>(class_name.size()));
    if (length > 0 &&
        wcscmp(class_name.data(), chatview::kHudWindowClassName) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HWND hud_window_for_process(DWORD process_id) noexcept
{
    if (process_id == 0U) {
        return nullptr;
    }

    WindowSearch search{process_id, nullptr};
    EnumWindows(&find_hud_window, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

std::wstring output_description(
    const chatview::ControlStatusSnapshot &snapshot)
{
    std::vector<std::wstring> outputs;
    if (chatview::has_control_status_flag(
            snapshot, chatview::ControlStatusStreaming)) {
        outputs.emplace_back(L"Streaming");
    }
    if (chatview::has_control_status_flag(
            snapshot, chatview::ControlStatusRecording)) {
        outputs.emplace_back(L"Recording");
    }
    if (chatview::has_control_status_flag(
            snapshot, chatview::ControlStatusReplayBuffer)) {
        outputs.emplace_back(L"Replay buffer");
    }
    if (chatview::has_control_status_flag(
            snapshot, chatview::ControlStatusVirtualCamera)) {
        outputs.emplace_back(L"Virtual camera");
    }

    if (outputs.empty()) {
        return L"Idle";
    }

    std::wstring result;
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        if (index != 0U) {
            result.append(L"  •  ");
        }
        result.append(outputs[index]);
    }
    return result;
}

class ControlCenterWindow final {
public:
    explicit ControlCenterWindow(Options options)
        : options_(std::move(options)), dpi_(system_dpi())
    {
    }

    ~ControlCenterWindow()
    {
        destroy_fonts();
    }

    [[nodiscard]] bool create(HINSTANCE instance)
    {
        instance_ = instance;
        activation_message_ = RegisterWindowMessageW(
            chatview::kControlCenterActivateMessageName);
        if (activation_message_ == 0U) {
            return false;
        }

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &ControlCenterWindow::window_proc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        window_class.lpszClassName = kWindowClassName;

        if (RegisterClassExW(&window_class) == 0U &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        const int width = scale_dip(kWindowWidthDip, dpi_);
        const int height = scale_dip(kWindowHeightDip, dpi_);
        window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kWindowClassName,
            L"ChatView Control Center",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            width,
            height,
            nullptr,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr) {
            return false;
        }

        center_on_primary_monitor();
        ShowWindow(window_, SW_SHOWNORMAL);
        SetForegroundWindow(window_);
        return true;
    }

private:
    static LRESULT CALLBACK window_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        ControlCenterWindow *self =
            reinterpret_cast<ControlCenterWindow *>(
                GetWindowLongPtrW(window, GWLP_USERDATA));

        if (message == WM_NCCREATE) {
            const auto *create =
                reinterpret_cast<const CREATESTRUCTW *>(lparam);
            self = static_cast<ControlCenterWindow *>(
                create->lpCreateParams);
            SetWindowLongPtrW(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        }

        if (self == nullptr) {
            return DefWindowProcW(window, message, wparam, lparam);
        }

        try {
            return self->handle_message(message, wparam, lparam);
        } catch (...) {
            MessageBoxW(
                window,
                L"ChatView Control Center encountered an unexpected error.",
                L"ChatView Control Center",
                MB_OK | MB_ICONERROR);
            DestroyWindow(window);
            return 0L;
        }
    }

    LRESULT handle_message(
        UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (activation_message_ != 0U &&
            message == activation_message_) {
            if (static_cast<DWORD>(wparam) ==
                options_.parent_process_id) {
                ShowWindow(window_, SW_RESTORE);
                SetForegroundWindow(window_);
            }
            return 0L;
        }

        switch (message) {
        case WM_CREATE:
            if (!create_controls()) {
                return -1L;
            }
            connect_to_obs();
            refresh_provider();
            refresh_runtime_status(true);
            if (SetTimer(
                    window_,
                    kRefreshTimerId,
                    kRefreshIntervalMs,
                    nullptr) == 0U) {
                return -1L;
            }
            return 0L;
        case WM_COMMAND:
            if (LOWORD(wparam) == kUrlEditId &&
                HIWORD(wparam) == EN_CHANGE) {
                refresh_provider();
                return 0L;
            }
            switch (LOWORD(wparam)) {
            case kSaveButtonId:
                save_and_apply();
                return 0L;
            case kEditButtonId:
                toggle_overlay_edit();
                return 0L;
            case kRestartButtonId:
                restart_hud();
                return 0L;
            case kCloseButtonId:
                DestroyWindow(window_);
                return 0L;
            default:
                break;
            }
            break;
        case WM_TIMER:
            if (wparam == kRefreshTimerId) {
                refresh_runtime_status(false);
                return 0L;
            }
            break;
        case WM_CTLCOLORSTATIC:
            return color_static(
                reinterpret_cast<HDC>(wparam),
                reinterpret_cast<HWND>(lparam));
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wparam);
            const RECT *suggested =
                reinterpret_cast<const RECT *>(lparam);
            SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            create_fonts();
            layout_controls();
            return 0L;
        }
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0L;
        case WM_DESTROY:
            KillTimer(window_, kRefreshTimerId);
            status_reader_.close();
            PostQuitMessage(0);
            return 0L;
        default:
            break;
        }

        return DefWindowProcW(window_, message, wparam, lparam);
    }

    bool create_controls()
    {
        title_ = create_static(L"ChatView Control Center");
        subtitle_ = create_static(
            L"Configure chat and verify the private HUD before going live.");
        url_label_ = create_static(L"Broadcast or chat URL");
        url_edit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kUrlEditId)),
            instance_,
            nullptr);
        provider_value_ = create_static(L"Not configured");
        status_group_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"System status",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            0,
            0,
            0,
            0,
            window_,
            nullptr,
            instance_,
            nullptr);

        obs_label_ = create_static(L"OBS connection");
        obs_value_ = create_static(L"Checking...");
        hud_label_ = create_static(L"HUD runtime");
        hud_value_ = create_static(L"Checking...");
        safety_label_ = create_static(L"Capture safety");
        safety_value_ = create_static(L"Checking...");
        output_label_ = create_static(L"OBS output");
        output_value_ = create_static(L"Checking...");
        feedback_ = create_static(L"");
        version_ = create_static(
            L"ChatView " CHATVIEW_WIDEN(CHATVIEW_VERSION));

        save_button_ = create_button(
            L"Save & Apply", kSaveButtonId, BS_DEFPUSHBUTTON);
        edit_button_ = create_button(
            L"Move / Resize", kEditButtonId, BS_PUSHBUTTON);
        restart_button_ = create_button(
            L"Restart HUD", kRestartButtonId, BS_PUSHBUTTON);
        close_button_ = create_button(
            L"Close", kCloseButtonId, BS_PUSHBUTTON);

        const std::array<HWND, 18U> required{
            title_, subtitle_, url_label_, url_edit_, provider_value_,
            status_group_, obs_label_, obs_value_, hud_label_, hud_value_,
            safety_label_, safety_value_, output_label_, output_value_,
            feedback_, save_button_, edit_button_, restart_button_};
        if (std::any_of(
                required.begin(), required.end(), [](HWND control) {
                    return control == nullptr;
                }) ||
            close_button_ == nullptr || version_ == nullptr) {
            return false;
        }

        create_fonts();
        layout_controls();

        chatview::ChatConfig config;
        if (chatview::load_chat_config(config)) {
            SetWindowTextW(url_edit_, config.url.c_str());
        }
        SendMessageW(url_edit_, EM_SETLIMITTEXT, kMaximumUrlLength, 0L);
        SetFocus(url_edit_);
        return true;
    }

    HWND create_static(const wchar_t *text) const
    {
        return CreateWindowExW(
            0,
            L"STATIC",
            text,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0,
            0,
            0,
            0,
            window_,
            nullptr,
            instance_,
            nullptr);
    }

    HWND create_button(
        const wchar_t *text, int identifier, DWORD button_style) const
    {
        return CreateWindowExW(
            0,
            L"BUTTON",
            text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | button_style,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(identifier)),
            instance_,
            nullptr);
    }

    void connect_to_obs()
    {
        connected_ = options_.connected() &&
                     status_reader_.open(
                         options_.status_mapping_name,
                         options_.status_event_name,
                         options_.restart_event_name,
                         options_.parent_process_id);
    }

    void refresh_provider()
    {
        const std::wstring input = trim(control_text(url_edit_));
        if (input.empty()) {
            set_colored_text(
                provider_value_,
                L"●  Not configured",
                kColorMuted,
                provider_color_);
            return;
        }

        const std::wstring normalized =
            chatview::normalize_chat_url(input);
        if (normalized.empty()) {
            set_colored_text(
                provider_value_,
                L"●  Unsupported URL",
                kColorError,
                provider_color_);
            return;
        }

        set_colored_text(
            provider_value_,
            L"●  " + provider_name(normalized) +
                L" URL recognized",
            kColorGood,
            provider_color_);
    }

    void refresh_runtime_status(bool force)
    {
        if (!connected_ && options_.connected()) {
            connect_to_obs();
        }
        if (!connected_ || !status_reader_.parent_alive()) {
            connected_ = false;
            set_disconnected_status();
            return;
        }

        chatview::ControlStatusSnapshot snapshot;
        if (!status_reader_.read(snapshot)) {
            set_colored_text(
                obs_value_,
                L"●  Status unavailable",
                kColorError,
                obs_color_);
            set_colored_text(
                hud_value_, L"●  Unknown", kColorMuted, hud_color_);
            set_colored_text(
                safety_value_, L"●  Unknown", kColorMuted, safety_color_);
            set_colored_text(
                output_value_, L"●  Unknown", kColorMuted, output_color_);
            EnableWindow(edit_button_, FALSE);
            EnableWindow(restart_button_, FALSE);
            return;
        }
        if (!force && snapshot.generation == snapshot_.generation) {
            return;
        }
        snapshot_ = snapshot;

        set_colored_text(
            obs_value_,
            L"●  Connected to OBS Studio",
            kColorGood,
            obs_color_);

        const bool hud_running =
            chatview::has_control_status_flag(
                snapshot, chatview::ControlStatusHudRunning);
        const bool hud_visible =
            chatview::has_control_status_flag(
                snapshot, chatview::ControlStatusHudVisible);
        const bool capture_risk =
            chatview::has_control_status_flag(
                snapshot, chatview::ControlStatusCaptureRisk);

        if (hud_running && hud_visible) {
            set_colored_text(
                hud_value_,
                L"●  Running (PID " +
                    std::to_wstring(snapshot.hud_process_id) + L")",
                kColorGood,
                hud_color_);
        } else if (hud_running && capture_risk) {
            set_colored_text(
                hud_value_,
                L"●  Running, hidden by safety interlock",
                kColorWarning,
                hud_color_);
        } else if (hud_running) {
            set_colored_text(
                hud_value_,
                L"●  Running, window currently hidden",
                kColorWarning,
                hud_color_);
        } else {
            set_colored_text(
                hud_value_,
                L"●  Starting or recovering",
                kColorWarning,
                hud_color_);
        }

        if (capture_risk) {
            set_colored_text(
                safety_value_,
                L"●  HUD hidden: Display Capture risk detected",
                kColorWarning,
                safety_color_);
        } else {
            set_colored_text(
                safety_value_,
                L"●  Private HUD safety active",
                kColorGood,
                safety_color_);
        }

        set_colored_text(
            output_value_,
            L"●  " + output_description(snapshot),
            kColorText,
            output_color_);

        EnableWindow(
            edit_button_,
            hud_running && !capture_risk ? TRUE : FALSE);
        EnableWindow(restart_button_, TRUE);
    }

    void set_disconnected_status()
    {
        set_colored_text(
            obs_value_,
            L"●  Not connected — open from OBS Tools menu",
            kColorError,
            obs_color_);
        set_colored_text(
            hud_value_, L"●  Unknown", kColorMuted, hud_color_);
        set_colored_text(
            safety_value_, L"●  Unknown", kColorMuted, safety_color_);
        set_colored_text(
            output_value_, L"●  Unknown", kColorMuted, output_color_);
        EnableWindow(edit_button_, FALSE);
        EnableWindow(restart_button_, FALSE);
    }

    void save_and_apply()
    {
        std::wstring input = trim(control_text(url_edit_));
        if (static_cast<int>(input.size()) < kMinimumUrlLength ||
            static_cast<int>(input.size()) > kMaximumUrlLength) {
            set_feedback(
                L"The URL is too long.", kColorError);
            return;
        }

        bool saved = false;
        if (input.empty()) {
            saved = chatview::clear_chat_config();
        } else {
            const std::wstring normalized =
                chatview::normalize_chat_url(input);
            if (normalized.empty()) {
                set_feedback(
                    L"Paste a supported Weflab, CHZZK, SOOP, or YouTube HTTPS URL.",
                    kColorError);
                return;
            }
            saved = chatview::save_chat_config(
                chatview::ChatConfig{normalized});
            if (saved) {
                SetWindowTextW(url_edit_, normalized.c_str());
            }
        }

        if (!saved) {
            set_feedback(
                L"ChatView could not save the configuration.",
                kColorError);
            return;
        }

        const UINT message = RegisterWindowMessageW(
            chatview::kConfigChangedMessageName);
        if (message != 0U) {
            SendNotifyMessageW(HWND_BROADCAST, message, 0U, 0L);
        }
        refresh_provider();
        set_feedback(
            connected_
                ? L"Saved and applied to the running HUD."
                : L"Saved. Open ChatView from OBS to apply it.",
            kColorGood);
    }

    void toggle_overlay_edit()
    {
        if (!connected_ || !status_reader_.parent_alive()) {
            set_feedback(
                L"OBS is not connected.", kColorError);
            return;
        }

        chatview::ControlStatusSnapshot snapshot;
        if (!status_reader_.read(snapshot) ||
            !chatview::has_control_status_flag(
                snapshot, chatview::ControlStatusHudRunning) ||
            chatview::has_control_status_flag(
                snapshot, chatview::ControlStatusCaptureRisk)) {
            set_feedback(
                L"The HUD is not available for editing.",
                kColorWarning);
            return;
        }

        const HWND hud = hud_window_for_process(
            snapshot.hud_process_id);
        const UINT message = RegisterWindowMessageW(
            chatview::kToggleEditMessageName);
        if (hud == nullptr || message == 0U ||
            !PostMessageW(hud, message, 0U, 0L)) {
            set_feedback(
                L"ChatView could not enter overlay edit mode.",
                kColorError);
            return;
        }

        set_feedback(
            L"Overlay edit mode toggled.", kColorGood);
    }

    void restart_hud()
    {
        if (!connected_ || !status_reader_.request_restart()) {
            set_feedback(
                L"The HUD restart request could not be delivered.",
                kColorError);
            return;
        }

        set_feedback(
            L"HUD restart requested.", kColorGood);
    }

    void set_feedback(
        const std::wstring &text, COLORREF color)
    {
        set_colored_text(
            feedback_, text, color, feedback_color_);
    }

    void set_colored_text(
        HWND control,
        const std::wstring &text,
        COLORREF color,
        COLORREF &stored_color)
    {
        stored_color = color;
        SetWindowTextW(control, text.c_str());
        InvalidateRect(control, nullptr, TRUE);
    }

    LRESULT color_static(HDC device, HWND control) const
    {
        COLORREF color = kColorText;
        if (control == subtitle_ || control == version_) {
            color = kColorMuted;
        } else if (control == provider_value_) {
            color = provider_color_;
        } else if (control == obs_value_) {
            color = obs_color_;
        } else if (control == hud_value_) {
            color = hud_color_;
        } else if (control == safety_value_) {
            color = safety_color_;
        } else if (control == output_value_) {
            color = output_color_;
        } else if (control == feedback_) {
            color = feedback_color_;
        }

        SetTextColor(device, color);
        SetBkMode(device, TRANSPARENT);
        return reinterpret_cast<LRESULT>(
            GetSysColorBrush(COLOR_WINDOW));
    }

    void create_fonts()
    {
        destroy_fonts();
        title_font_ = CreateFontW(
            -scale_dip(25, dpi_),
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        body_font_ = CreateFontW(
            -scale_dip(15, dpi_),
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        label_font_ = CreateFontW(
            -scale_dip(15, dpi_),
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");

        const std::array<HWND, 19U> body_controls{
            subtitle_, url_edit_, provider_value_, status_group_,
            obs_value_, hud_value_, safety_value_, output_value_, feedback_,
            save_button_, edit_button_, restart_button_, close_button_,
            version_, url_label_, obs_label_, hud_label_, safety_label_,
            output_label_};
        for (HWND control : body_controls) {
            if (control != nullptr) {
                SendMessageW(
                    control,
                    WM_SETFONT,
                    reinterpret_cast<WPARAM>(body_font_),
                    TRUE);
            }
        }

        SendMessageW(
            title_,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(title_font_),
            TRUE);
        const std::array<HWND, 5U> labels{
            url_label_, obs_label_, hud_label_, safety_label_, output_label_};
        for (HWND label : labels) {
            SendMessageW(
                label,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(label_font_),
                TRUE);
        }
    }

    void destroy_fonts() noexcept
    {
        if (title_font_ != nullptr) {
            DeleteObject(title_font_);
            title_font_ = nullptr;
        }
        if (body_font_ != nullptr) {
            DeleteObject(body_font_);
            body_font_ = nullptr;
        }
        if (label_font_ != nullptr) {
            DeleteObject(label_font_);
            label_font_ = nullptr;
        }
    }

    void layout_controls()
    {
        const auto move = [this](
                              HWND control,
                              int x,
                              int y,
                              int width,
                              int height) {
            MoveWindow(
                control,
                scale_dip(x, dpi_),
                scale_dip(y, dpi_),
                scale_dip(width, dpi_),
                scale_dip(height, dpi_),
                TRUE);
        };

        move(title_, 32, 24, 756, 38);
        move(subtitle_, 34, 61, 752, 24);
        move(url_label_, 34, 105, 300, 22);
        move(url_edit_, 34, 132, 752, 32);
        move(provider_value_, 36, 170, 748, 24);
        move(status_group_, 28, 207, 764, 190);
        move(obs_label_, 52, 239, 190, 24);
        move(obs_value_, 248, 239, 510, 24);
        move(hud_label_, 52, 275, 190, 24);
        move(hud_value_, 248, 275, 510, 24);
        move(safety_label_, 52, 311, 190, 24);
        move(safety_value_, 248, 311, 510, 24);
        move(output_label_, 52, 347, 190, 24);
        move(output_value_, 248, 347, 510, 24);
        move(feedback_, 34, 407, 450, 24);
        move(save_button_, 34, 443, 142, 38);
        move(edit_button_, 188, 443, 142, 38);
        move(restart_button_, 342, 443, 142, 38);
        move(close_button_, 644, 443, 142, 38);
        move(version_, 500, 409, 286, 22);
    }

    void center_on_primary_monitor()
    {
        RECT window_rect{};
        if (!GetWindowRect(window_, &window_rect)) {
            return;
        }

        const HMONITOR monitor = MonitorFromWindow(
            window_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!GetMonitorInfoW(monitor, &monitor_info)) {
            return;
        }

        const int width = window_rect.right - window_rect.left;
        const int height = window_rect.bottom - window_rect.top;
        const int x = monitor_info.rcWork.left +
                      (monitor_info.rcWork.right -
                       monitor_info.rcWork.left - width) /
                          2;
        const int y = monitor_info.rcWork.top +
                      (monitor_info.rcWork.bottom -
                       monitor_info.rcWork.top - height) /
                          2;
        SetWindowPos(
            window_,
            nullptr,
            x,
            y,
            0,
            0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    Options options_;
    chatview::ControlStatusReader status_reader_;
    chatview::ControlStatusSnapshot snapshot_;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND title_ = nullptr;
    HWND subtitle_ = nullptr;
    HWND url_label_ = nullptr;
    HWND url_edit_ = nullptr;
    HWND provider_value_ = nullptr;
    HWND status_group_ = nullptr;
    HWND obs_label_ = nullptr;
    HWND obs_value_ = nullptr;
    HWND hud_label_ = nullptr;
    HWND hud_value_ = nullptr;
    HWND safety_label_ = nullptr;
    HWND safety_value_ = nullptr;
    HWND output_label_ = nullptr;
    HWND output_value_ = nullptr;
    HWND feedback_ = nullptr;
    HWND version_ = nullptr;
    HWND save_button_ = nullptr;
    HWND edit_button_ = nullptr;
    HWND restart_button_ = nullptr;
    HWND close_button_ = nullptr;
    HFONT title_font_ = nullptr;
    HFONT body_font_ = nullptr;
    HFONT label_font_ = nullptr;
    UINT activation_message_ = 0U;
    UINT dpi_ = 96U;
    bool connected_ = false;
    COLORREF provider_color_ = kColorMuted;
    COLORREF obs_color_ = kColorMuted;
    COLORREF hud_color_ = kColorMuted;
    COLORREF safety_color_ = kColorMuted;
    COLORREF output_color_ = kColorMuted;
    COLORREF feedback_color_ = kColorMuted;
};

std::wstring instance_mutex_name(const Options &options)
{
    return L"Local\\ChatViewOBS.ControlCenter." +
           (options.parent_process_id == 0U
                ? std::wstring(L"Standalone")
                : std::to_wstring(options.parent_process_id));
}

void activate_existing_instance(const Options &options) noexcept
{
    const UINT message = RegisterWindowMessageW(
        chatview::kControlCenterActivateMessageName);
    if (message != 0U) {
        SendNotifyMessageW(
            HWND_BROADCAST,
            message,
            static_cast<WPARAM>(options.parent_process_id),
            0L);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try {
        enable_per_monitor_dpi_awareness();

        Options options;
        if (!parse_options(options)) {
            MessageBoxW(
                nullptr,
                L"ChatView Control Center received an invalid command line.",
                L"ChatView Control Center",
                MB_OK | MB_ICONERROR);
            return 2;
        }

        const std::wstring mutex_name = instance_mutex_name(options);
        chatview::UniqueHandle instance_mutex(
            CreateMutexW(nullptr, TRUE, mutex_name.c_str()));
        if (!instance_mutex) {
            MessageBoxW(
                nullptr,
                L"ChatView could not create its Control Center lock.",
                L"ChatView Control Center",
                MB_OK | MB_ICONERROR);
            return 1;
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            activate_existing_instance(options);
            return 0;
        }

        ControlCenterWindow window(std::move(options));
        if (!window.create(instance)) {
            MessageBoxW(
                nullptr,
                L"ChatView Control Center could not be opened.",
                L"ChatView Control Center",
                MB_OK | MB_ICONERROR);
            return 1;
        }

        MSG message{};
        int result = 0;
        while ((result = GetMessageW(
                    &message, nullptr, 0U, 0U)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return result < 0 ? 3 : static_cast<int>(message.wParam);
    } catch (...) {
        MessageBoxW(
            nullptr,
            L"ChatView Control Center encountered an unexpected error.",
            L"ChatView Control Center",
            MB_OK | MB_ICONERROR);
        return 4;
    }
}
