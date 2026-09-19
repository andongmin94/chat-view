// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/win32-handle.hpp"
#include "hud/hud-window.hpp"
#include "hud/launch-options.hpp"
#include "hud/native-chat-connection.hpp"
#include "hud/shared-state-reader.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

class ComSession final {
public:
    ComSession() = default;

    [[nodiscard]] bool start() noexcept
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        initialized_ = SUCCEEDED(result);
        return initialized_;
    }

    ~ComSession()
    {
        if (initialized_) {
            CoUninitialize();
        }
    }

    ComSession(const ComSession &) = delete;
    ComSession &operator=(const ComSession &) = delete;

private:
    bool initialized_ = false;
};

constexpr int kCompanionQuitHotkey = 0x4351;

class CompanionQuitHotkey final {
public:
    bool start() noexcept
    {
        registered_ = RegisterHotKey(nullptr, kCompanionQuitHotkey,
            MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q') != FALSE;
        return registered_;
    }
    ~CompanionQuitHotkey()
    {
        if (registered_) UnregisterHotKey(nullptr, kCompanionQuitHotkey);
    }
private:
    bool registered_ = false;
};

void enable_per_monitor_dpi_awareness()
{
    using SetProcessDpiAwarenessContextFunction = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);

    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto set_process_dpi_awareness_context =
        reinterpret_cast<SetProcessDpiAwarenessContextFunction>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (set_process_dpi_awareness_context != nullptr) {
        set_process_dpi_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

std::optional<chatview::HudLaunchOptions> parse_options()
{
    int argument_count = 0;
    // Release the shell allocation even if an owning string allocation fails.
    const std::unique_ptr<wchar_t *, decltype(&LocalFree)> arguments(
        CommandLineToArgvW(GetCommandLineW(), &argument_count), &LocalFree);
    if (!arguments || argument_count < 1) return std::nullopt;
    std::vector<std::wstring_view> values;
    values.reserve(static_cast<std::size_t>(argument_count - 1));
    for (int index = 1; index < argument_count; ++index)
        values.emplace_back(arguments.get()[index]);
    return chatview::parse_hud_launch_options(values);
}

bool dispatch_pending_messages(int &exit_code, chatview::NativeChatConnection &chat,
                               bool companion)
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            exit_code = static_cast<int>(message.wParam);
            return false;
        }
        if (companion && !message.hwnd && message.message == WM_HOTKEY &&
            message.wParam == kCompanionQuitHotkey) {
            exit_code = 0;
            return false;
        }
        if (chat.dispatch(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

int run(HINSTANCE instance, const chatview::HudLaunchOptions &options)
{
    const bool companion = options.mode == chatview::HudLaunchMode::Companion;
    chatview::UniqueHandle companion_instance;
    CompanionQuitHotkey quit_hotkey;
    if (companion) {
        const HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\ChatView.Companion");
        const DWORD error = GetLastError();
        companion_instance.reset(mutex);
        if (!companion_instance) return 12;
        if (error == ERROR_ALREADY_EXISTS) return 13;
        // This is not the unqualified dual-PC broadcasting product. Never
        // silently start a private HUD over a composited HDMI broadcast.
        const int consent = MessageBoxW(nullptr,
            L"OBS 없이 HUD를 실행하는 개발 검증 모드입니다.\n\n"
            L"이 모드는 송출 PC의 캡처 상태를 감지하지 않습니다.\n"
            L"화면 복제·HDMI에는 개인 채팅이 그대로 나갈 수 있습니다.\n"
            L"영상 경로를 검증하기 전에는 실제 방송에 사용하지 마세요.\n\n"
            L"연결: Ctrl+Alt+Shift+C / 이동: Ctrl+Alt+Shift+H\n"
            L"챗뷰 종료: Ctrl+Alt+Shift+Q\n\n개발 검증을 계속할까요?",
            L"ChatView · 독립 HUD (개발 검증)",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (consent != IDYES) return 0;
        if (!quit_hotkey.start()) {
            MessageBoxW(nullptr, L"종료 단축키를 등록하지 못해 실행하지 않습니다.",
                L"ChatView", MB_OK | MB_ICONERROR);
            return 14;
        }
    }

    chatview::SharedStateReader state_reader;
    if (!companion && !state_reader.open(
            options.mapping_name, options.event_name, options.parent_process_id)) {
        OutputDebugStringW(L"[ChatView HUD] Failed to open controller transport\n");
        return 2;
    }

    chatview::UniqueHandle ready_event;
    if (companion) {
        ready_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!ready_event) return 6;
    } else if (!options.ready_event_name.empty()) {
        ready_event.reset(OpenEventW(
            EVENT_MODIFY_STATE, FALSE, options.ready_event_name.c_str()));
        if (!ready_event) {
            OutputDebugStringW(L"[ChatView HUD] Failed to open readiness event\n");
            return 6;
        }
    }

    chatview::HudWindow hud_window;
    if (!hud_window.create(instance, ready_event.get())) {
        return 3;
    }

    chatview::NativeChatConnection chat(hud_window);
    if (!companion) hud_window.show_ready();

    chatview::SharedSnapshot initial_snapshot;
    if (!companion && state_reader.read(initial_snapshot)) {
        hud_window.apply_state(initial_snapshot);
    }

    HANDLE wait_handles[2] = {
        companion ? ready_event.get() : state_reader.state_changed_event(),
        companion ? nullptr : state_reader.parent_process(),
    };
    DWORD handle_count = wait_handles[1] != nullptr ? 2U : 1U;
    bool open_companion_panel = companion;

    int exit_code = 0;
    bool running = true;
    while (running) {
        if (!dispatch_pending_messages(exit_code, chat, companion)) {
            break;
        }

        chat.tick();
        const DWORD wait_result = MsgWaitForMultipleObjectsEx(
            handle_count,
            wait_handles,
            chat.wait_timeout(),
            QS_ALLINPUT,
            MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
        if (wait_result == WAIT_FAILED) {
            exit_code = 7;
            break;
        }

        if (wait_result == WAIT_IO_COMPLETION || wait_result == WAIT_TIMEOUT) {
            continue;
        }

        if (open_companion_panel && wait_result == WAIT_OBJECT_0) {
            open_companion_panel = false;
            handle_count = 0U;
            wait_handles[0] = nullptr;
            chat.open_dialog();
            continue;
        }

        if (!companion && wait_result == WAIT_OBJECT_0) {
            chatview::SharedSnapshot snapshot;
            if (state_reader.read(snapshot)) {
                hud_window.apply_state(snapshot);
            }
            continue;
        }

        if (!companion && handle_count == 2U && wait_result == WAIT_OBJECT_0 + 1U) {
            break;
        }

        if (wait_result == WAIT_OBJECT_0 + handle_count) {
            continue;
        }

        exit_code = 8;
        running = false;
    }

    chat.close();
    hud_window.destroy();
    return exit_code;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try {
        enable_per_monitor_dpi_awareness();

        const auto options = parse_options();
        if (!options) {
            OutputDebugStringW(L"[ChatView HUD] Invalid command line\n");
            return 1;
        }

        ComSession com;
        if (!com.start()) {
            OutputDebugStringW(L"[ChatView HUD] Failed to initialize COM\n");
            return 4;
        }

        return run(instance, *options);
    } catch (const std::exception &error) {
        OutputDebugStringA(error.what());
        OutputDebugStringW(L"\n[ChatView HUD] Unhandled exception\n");
    } catch (...) {
        OutputDebugStringW(L"[ChatView HUD] Unknown unhandled exception\n");
    }

    return 5;
}
