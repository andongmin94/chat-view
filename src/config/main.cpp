// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/chat-config.hpp"
#include "common/win32-handle.hpp"
#include "common/window-messages.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <utility>

namespace {

constexpr wchar_t kWindowClassName[] = L"ChatViewObsConfigWindow";
constexpr wchar_t kInstanceMutexName[] = L"Local\\ChatViewOBS.Settings.v1";
constexpr int kUrlEditId = 1001;
constexpr int kSaveButtonId = 1002;
constexpr int kCancelButtonId = 1003;

std::wstring trim(std::wstring value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t character) {
        return std::iswspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character) {
                          return std::iswspace(character) != 0;
                      }).base();

    if (first >= last) {
        return {};
    }
    return std::wstring(first, last);
}

void apply_default_font(HWND control)
{
    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
        TRUE);
}

void broadcast_config_changed()
{
    const UINT message = RegisterWindowMessageW(chatview::kConfigChangedMessageName);
    if (message != 0U) {
        SendNotifyMessageW(HWND_BROADCAST, message, 0U, 0L);
    }
}

void activate_existing_window() noexcept
{
    for (unsigned int attempt = 0U; attempt < 40U; ++attempt) {
        const HWND existing = FindWindowW(kWindowClassName, nullptr);
        if (existing != nullptr) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
            return;
        }
        Sleep(50U);
    }
}

class ConfigWindow final {
public:
    [[nodiscard]] bool create(HINSTANCE instance)
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &ConfigWindow::window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        window_class.lpszClassName = kWindowClassName;

        if (RegisterClassExW(&window_class) == 0U &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kWindowClassName,
            L"ChatView Settings",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            730,
            270,
            nullptr,
            nullptr,
            instance,
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
        ConfigWindow *self =
            reinterpret_cast<ConfigWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));

        if (message == WM_NCCREATE) {
            const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
            self = static_cast<ConfigWindow *>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        }

        return self != nullptr ? self->handle_message(message, wparam, lparam)
                               : DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam)
    {
        switch (message) {
        case WM_CREATE:
            return create_controls() ? 0L : -1L;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case kSaveButtonId:
                save();
                return 0L;
            case kCancelButtonId:
                DestroyWindow(window_);
                return 0L;
            default:
                break;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0L;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0L;
        default:
            break;
        }

        return DefWindowProcW(window_, message, wparam, lparam);
    }

    bool create_controls()
    {
        HWND label = CreateWindowExW(
            0,
            L"STATIC",
            L"Broadcast or chat URL",
            WS_CHILD | WS_VISIBLE,
            20,
            18,
            670,
            20,
            window_,
            nullptr,
            nullptr,
            nullptr);
        url_edit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            20,
            42,
            670,
            28,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUrlEditId)),
            nullptr,
            nullptr);
        HWND hint = CreateWindowExW(
            0,
            L"STATIC",
            L"Paste a Weflab page, CHZZK channel/live/chat URL, or YouTube watch/live/chat URL.\nChatView converts normal CHZZK and YouTube broadcast links automatically. Leave empty to disable chat.",
            WS_CHILD | WS_VISIBLE,
            20,
            80,
            670,
            50,
            window_,
            nullptr,
            nullptr,
            nullptr);
        HWND save_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"Save",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            510,
            168,
            85,
            30,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveButtonId)),
            nullptr,
            nullptr);
        HWND cancel_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            605,
            168,
            85,
            30,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelButtonId)),
            nullptr,
            nullptr);

        if (label == nullptr || url_edit_ == nullptr || hint == nullptr ||
            save_button == nullptr || cancel_button == nullptr) {
            return false;
        }

        apply_default_font(label);
        apply_default_font(url_edit_);
        apply_default_font(hint);
        apply_default_font(save_button);
        apply_default_font(cancel_button);

        chatview::ChatConfig config;
        if (chatview::load_chat_config(config)) {
            SetWindowTextW(url_edit_, config.url.c_str());
        }

        SendMessageW(url_edit_, EM_SETSEL, 0U, -1L);
        SetFocus(url_edit_);
        return true;
    }

    void save()
    {
        const int length = GetWindowTextLengthW(url_edit_);
        if (length < 0 || length > 2048) {
            show_error(L"The URL is too long.");
            return;
        }

        std::wstring url(static_cast<std::size_t>(length) + 1U, L'\0');
        const int copied = GetWindowTextW(url_edit_, url.data(), length + 1);
        if (copied < 0) {
            show_error(L"The URL could not be read.");
            return;
        }
        url.resize(static_cast<std::size_t>(copied));
        url = trim(std::move(url));

        bool saved = false;
        if (url.empty()) {
            saved = chatview::clear_chat_config();
        } else {
            const std::wstring normalized = chatview::normalize_chat_url(url);
            if (normalized.empty()) {
                show_error(
                    L"Paste a supported HTTPS Weflab, CHZZK, or YouTube broadcast/chat URL.");
                return;
            }
            saved = chatview::save_chat_config(chatview::ChatConfig{normalized});
        }

        if (!saved) {
            show_error(L"ChatView could not save the settings.");
            return;
        }

        broadcast_config_changed();
        DestroyWindow(window_);
    }

    void show_error(const wchar_t *message) const
    {
        MessageBoxW(window_, message, L"ChatView Settings", MB_OK | MB_ICONERROR);
    }

    void center_on_primary_monitor()
    {
        RECT window_rect{};
        if (!GetWindowRect(window_, &window_rect)) {
            return;
        }

        const HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!GetMonitorInfoW(monitor, &monitor_info)) {
            return;
        }

        const int width = window_rect.right - window_rect.left;
        const int height = window_rect.bottom - window_rect.top;
        const int x = monitor_info.rcWork.left +
                      (monitor_info.rcWork.right - monitor_info.rcWork.left - width) / 2;
        const int y = monitor_info.rcWork.top +
                      (monitor_info.rcWork.bottom - monitor_info.rcWork.top - height) / 2;
        SetWindowPos(
            window_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    HWND window_ = nullptr;
    HWND url_edit_ = nullptr;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    chatview::UniqueHandle instance_mutex(
        CreateMutexW(nullptr, TRUE, kInstanceMutexName));
    if (!instance_mutex) {
        MessageBoxW(
            nullptr,
            L"ChatView could not create its settings lock.",
            L"ChatView Settings",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        activate_existing_window();
        return 0;
    }

    ConfigWindow window;
    if (!window.create(instance)) {
        MessageBoxW(
            nullptr,
            L"ChatView settings could not be opened.",
            L"ChatView Settings",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    MSG message{};
    int message_result = 0;
    while ((message_result = GetMessageW(&message, nullptr, 0U, 0U)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return message_result < 0 ? 2 : static_cast<int>(message.wParam);
}
