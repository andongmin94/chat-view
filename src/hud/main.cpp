// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/win32-handle.hpp"
#include "hud/hud-window.hpp"
#include "hud/shared-state-reader.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <cerrno>
#include <cstdlib>
#include <exception>
#include <limits>
#include <string>

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

struct Options {
    std::wstring mapping_name;
    std::wstring event_name;
    std::wstring ready_event_name;
    DWORD parent_process_id = 0U;
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

bool parse_parent_process_id(const wchar_t *text, DWORD &process_id) noexcept
{
    errno = 0;
    wchar_t *end = nullptr;
    const unsigned long value = std::wcstoul(text, &end, 10);
    if (errno == ERANGE || end == nullptr || end == text || *end != L'\0' ||
        value == 0UL || value > std::numeric_limits<DWORD>::max()) {
        return false;
    }

    process_id = static_cast<DWORD>(value);
    return true;
}

bool parse_options(Options &options)
{
    int argument_count = 0;
    wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        return false;
    }

    bool valid = true;
    for (int index = 1; index < argument_count && valid; ++index) {
        const std::wstring argument = arguments[index];
        if (argument == L"--mapping" && index + 1 < argument_count) {
            options.mapping_name = arguments[++index];
        } else if (argument == L"--event" && index + 1 < argument_count) {
            options.event_name = arguments[++index];
        } else if (argument == L"--ready-event" && index + 1 < argument_count) {
            options.ready_event_name = arguments[++index];
        } else if (argument == L"--parent" && index + 1 < argument_count) {
            valid = parse_parent_process_id(arguments[++index], options.parent_process_id);
        } else {
            valid = false;
        }
    }

    LocalFree(arguments);
    return valid && !options.mapping_name.empty() && !options.event_name.empty() &&
           options.parent_process_id != 0U;
}

int run(HINSTANCE instance, const Options &options)
{
    chatview::SharedStateReader state_reader;
    if (!state_reader.open(
            options.mapping_name, options.event_name, options.parent_process_id)) {
        OutputDebugStringW(L"[ChatView HUD] Failed to open controller transport\n");
        return 2;
    }

    chatview::UniqueHandle ready_event;
    if (!options.ready_event_name.empty()) {
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

    hud_window.show_ready();

    chatview::SharedSnapshot initial_snapshot;
    if (state_reader.read(initial_snapshot)) {
        hud_window.apply_state(initial_snapshot);
    }

    HANDLE wait_handles[2] = {
        state_reader.state_changed_event(),
        state_reader.parent_process(),
    };
    const DWORD handle_count = wait_handles[1] != nullptr ? 2U : 1U;

    int exit_code = 0;
    bool running = true;
    while (running) {
        const DWORD wait_result =
            MsgWaitForMultipleObjects(handle_count, wait_handles, FALSE, INFINITE, QS_ALLINPUT);
        if (wait_result == WAIT_FAILED) {
            exit_code = 7;
            break;
        }

        if (wait_result == WAIT_OBJECT_0) {
            chatview::SharedSnapshot snapshot;
            if (state_reader.read(snapshot)) {
                hud_window.apply_state(snapshot);
            }
            continue;
        }

        if (handle_count == 2U && wait_result == WAIT_OBJECT_0 + 1U) {
            break;
        }

        if (wait_result == WAIT_OBJECT_0 + handle_count) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    exit_code = static_cast<int>(message.wParam);
                    running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            continue;
        }

        break;
    }

    hud_window.destroy();
    return exit_code;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try {
        enable_per_monitor_dpi_awareness();

        Options options;
        if (!parse_options(options)) {
            OutputDebugStringW(L"[ChatView HUD] Invalid command line\n");
            return 1;
        }

        ComSession com;
        if (!com.start()) {
            OutputDebugStringW(L"[ChatView HUD] Failed to initialize COM\n");
            return 4;
        }

        return run(instance, options);
    } catch (const std::exception &error) {
        OutputDebugStringA(error.what());
        OutputDebugStringW(L"\n[ChatView HUD] Unhandled exception\n");
    } catch (...) {
        OutputDebugStringW(L"[ChatView HUD] Unknown unhandled exception\n");
    }

    return 5;
}
