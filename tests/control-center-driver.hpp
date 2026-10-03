// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Test-only controller transport and child process. The real Control Center
// talks to the production HUD in the test executable, not a fake query handler.
#include "common/control-status.hpp"
#include "common/native-chat-control.hpp"
#include "common/win32-handle.hpp"
#include <Windows.h>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

class ControlCenterDriver final {
public:
    ControlCenterDriver() = default;
    ControlCenterDriver(const ControlCenterDriver &) = delete;
    ControlCenterDriver &operator=(const ControlCenterDriver &) = delete;
    ~ControlCenterDriver()
    {
        if (process_ && WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT) {
            if (const auto hwnd = window()) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            if (WaitForSingleObject(process_.get(), 2000) == WAIT_TIMEOUT) {
                TerminateProcess(process_.get(), 1); WaitForSingleObject(process_.get(), 2000);
            }
        }
        if (status_) UnmapViewOfFile(status_);
    }
    void start(const std::filesystem::path &executable)
    {
        require(std::filesystem::is_regular_file(executable), "Control Center executable exists");
        const auto suffix = std::to_wstring(GetCurrentProcessId());
        const auto name = L"Local\\ChatView.Switch.Control." + suffix;
        const auto changed = name + L".Changed", restart = name + L".Restart";
        mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, static_cast<DWORD>(sizeof(chatview::ControlStatus)), name.c_str()));
        require(static_cast<bool>(mapping_), "create test controller mapping");
        status_ = static_cast<chatview::ControlStatus *>(MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS,
            0, 0, sizeof(chatview::ControlStatus)));
        require(status_ != nullptr, "map controller state");
        changed_.reset(CreateEventW(nullptr, FALSE, FALSE, changed.c_str()));
        restart_.reset(CreateEventW(nullptr, FALSE, FALSE, restart.c_str()));
        require(static_cast<bool>(changed_) && static_cast<bool>(restart_), "create controller events");
        ZeroMemory(status_, sizeof(*status_));
        status_->magic = chatview::kControlStatusMagic;
        status_->version = chatview::kControlStatusVersion;
        status_->last_hud_exit_code = chatview::kRuntimeExitCodeUnavailable;
        pulse();
        std::wstring command = L"\"" + executable.wstring() + L"\" --status-mapping \"" + name +
            L"\" --status-event \"" + changed + L"\" --restart-event \"" + restart +
            L"\" --parent " + suffix;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_UNICODE_ENVIRONMENT, nullptr, executable.parent_path().c_str(), &startup, &process) != FALSE,
            "launch actual Control Center");
        process_.reset(process.hProcess);
        chatview::UniqueHandle thread(process.hThread); pid_ = process.dwProcessId;
    }
    void pulse() noexcept
    {
        if (!status_) return;
        InterlockedIncrement(&status_->sequence); MemoryBarrier();
        status_->flags = chatview::ControlStatusHudRunning | chatview::ControlStatusHudVisible |
            chatview::ControlStatusSceneGraphReady;
        status_->hud_process_id = GetCurrentProcessId();
        ++status_->generation; status_->updated_tick_ms = GetTickCount64();
        MemoryBarrier(); InterlockedIncrement(&status_->sequence); SetEvent(changed_.get());
    }
    HWND window() const noexcept
    {
        struct Search { DWORD pid; HWND found; } search{pid_, nullptr};
        EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
            auto &s = *reinterpret_cast<Search *>(data);
            DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
            if (pid != s.pid) return TRUE;
            wchar_t name[64]{}; GetClassNameW(hwnd, name, 64);
            if (std::wstring_view(name) == L"ChatViewObsConfigWindow") { s.found = hwnd; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }
    bool matches(chatview::NativeChatStatus status) const
    {
        const HWND hwnd = window();
        return hwnd && text(hwnd, 1010).find(chatview::native_chat_status_text(status)) != std::wstring::npos;
    }
    bool video_warning() const
    {
        const HWND hwnd = window();
        return hwnd && IsWindowVisible(GetDlgItem(hwnd, 1008)) &&
            text(hwnd, 1008).find(L"Audience video: NOT VERIFIED") != std::wstring::npos;
    }
    void open_panel() const { click(1009); }
    void edit_external(const wchar_t *url) const
    {
        const HWND edit = GetDlgItem(window(), 1001);
        require(edit && SendMessageW(edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(url)),
            "edit external URL in the actual Control Center");
    }
    void apply_external() const { click(1002); }
    void request_close() const { require(PostMessageW(window(), WM_CLOSE, 0, 0) != FALSE, "close Control Center"); }
    bool exited() const noexcept { return process_ && WaitForSingleObject(process_.get(), 0) == WAIT_OBJECT_0; }
    bool succeeded() const noexcept
    {
        DWORD result = 1; return exited() && GetExitCodeProcess(process_.get(), &result) && result == 0;
    }
private:
    static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
    static std::wstring text(HWND hwnd, int id)
    {
        std::array<wchar_t, 1024> value{};
        GetDlgItemTextW(hwnd, id, value.data(), static_cast<int>(value.size()));
        return value.data();
    }
    void click(int id) const
    {
        const HWND hwnd = window(), control = GetDlgItem(hwnd, id);
        require(control && IsWindowEnabled(control), "Control Center action is available");
        // Asynchronous command: its handler can query this process's HUD.
        require(PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
            reinterpret_cast<LPARAM>(control)) != FALSE, "queue real Control Center action");
    }
    chatview::UniqueHandle mapping_, changed_, restart_, process_;
    chatview::ControlStatus *status_ = nullptr;
    DWORD pid_ = 0;
};
