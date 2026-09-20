// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual shipping entry point, not a fake OBS parent or HudWindow alone.
#include "common/win32-handle.hpp"
#include <Windows.h>
#include <TlHelp32.h>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr wchar_t kConsentTitle[] = L"ChatView · 독립 HUD (개발 검증)";
constexpr wchar_t kPanelTitle[] = L"ChatView · 자체 채팅 연결 (개발 검증)";
void expect(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
class Child final {
public:
    explicit Child(const wchar_t *executable)
    {
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --companion";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        expect(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                              0U, nullptr, nullptr, &startup, &info) != FALSE, "launch companion executable");
        process.reset(info.hProcess);
        thread.reset(info.hThread);
        pid = info.dwProcessId;
        thread_id = info.dwThreadId;
    }
    ~Child()
    {
        // Failure cleanup applies only to the process created by this test.
        if (WaitForSingleObject(process.get(), 0U) == WAIT_TIMEOUT) {
            TerminateProcess(process.get(), 99U);
            WaitForSingleObject(process.get(), 5000U);
        }
    }
    void expect_exit(DWORD expected)
    {
        expect(WaitForSingleObject(process.get(), 5000U) == WAIT_OBJECT_0, "bounded companion exit");
        DWORD code = 0U;
        expect(GetExitCodeProcess(process.get(), &code) && code == expected, "companion exit status");
    }
    chatview::UniqueHandle process;
    chatview::UniqueHandle thread;
    DWORD pid = 0U;
    DWORD thread_id = 0U;
};
struct WindowQuery { DWORD pid; const wchar_t *title; HWND found = nullptr; };
BOOL CALLBACK find_window(HWND window, LPARAM parameter)
{
    auto &query = *reinterpret_cast<WindowQuery *>(parameter);
    DWORD pid = 0U;
    GetWindowThreadProcessId(window, &pid);
    if (pid != query.pid || !IsWindowVisible(window)) return TRUE;
    wchar_t title[256]{};
    GetWindowTextW(window, title, 256);
    if (std::wcscmp(title, query.title) != 0) return TRUE;
    query.found = window;
    return FALSE;
}
HWND await_window(Child &child, const wchar_t *title)
{
    const ULONGLONG deadline = GetTickCount64() + 20000U;
    while (GetTickCount64() < deadline) {
        expect(WaitForSingleObject(child.process.get(), 0U) == WAIT_TIMEOUT, "companion remains alive without OBS");
        WindowQuery query{child.pid, title};
        EnumWindows(find_window, reinterpret_cast<LPARAM>(&query));
        if (query.found) return query.found;
        Sleep(20U);
    }
    throw std::runtime_error("companion consent/connection window not visible");
}
void choose(HWND dialog, int button)
{
    DWORD_PTR result = 0U;
    expect(SendMessageTimeoutW(dialog, WM_COMMAND, static_cast<WPARAM>(button), 0,
        SMTO_ABORTIFHUNG, 2000U, &result) != 0, "answer explicit development warning");
}
void expect_no_obs_module(DWORD pid)
{
    chatview::UniqueHandle snapshot;
    const ULONGLONG deadline = GetTickCount64() + 2000U;
    for (;;) {
        const HANDLE value = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (value != INVALID_HANDLE_VALUE) { snapshot.reset(value); break; }
        expect(GetLastError() == ERROR_BAD_LENGTH && GetTickCount64() < deadline, "inspect companion modules");
    }
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    expect(Module32FirstW(snapshot.get(), &module) != FALSE, "enumerate companion modules");
    do {
        expect(_wcsicmp(module.szModule, L"obs.dll") != 0 &&
               _wcsicmp(module.szModule, L"obs-frontend-api.dll") != 0,
               "companion does not load OBS libraries");
    } while (Module32NextW(snapshot.get(), &module));
    expect(GetLastError() == ERROR_NO_MORE_FILES, "module enumeration completed");
}
}
int wmain(int argc, wchar_t **argv)
{
    try {
        expect(argc == 2, "HUD executable required");
        wchar_t temporary[MAX_PATH]{};
        expect(GetTempPathW(MAX_PATH, temporary) != 0U, "temporary profile root");
        const std::wstring profile = std::wstring(temporary) + L"ChatView-Companion-" +
            std::to_wstring(GetCurrentProcessId());
        expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE, "isolated companion profile");
        expect(SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str()) != FALSE, "set isolated profile");
        {
            Child declined(argv[1]);
            choose(await_window(declined, kConsentTitle), IDNO);
            declined.expect_exit(0U);
        }
        Child child(argv[1]);
        choose(await_window(child, kConsentTitle), IDYES);
        const HWND panel = await_window(child, kPanelTitle);
        expect(GetDlgItem(panel, 101) && GetDlgItem(panel, 107) && GetDlgItem(panel, 104),
               "existing authenticated connection controls available");
        expect(GetDlgItem(panel, 102) == nullptr, "no manual credential field in connection panel");
        await_window(child, L"ChatView HUD");
        expect_no_obs_module(child.pid);
        {
            Child duplicate(argv[1]);
            duplicate.expect_exit(13U);
        }
        expect(PostThreadMessageW(child.thread_id, WM_HOTKEY, 0x4351U,
            MAKELPARAM(MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'Q')) != FALSE,
            "request normal companion shutdown");
        child.expect_exit(0U);
        std::cout << "Companion entry, consent, connection UI, no OBS modules, duplicate and exit passed\n";
    } catch (const std::exception &error) {
        std::cerr << "Companion test failed: " << error.what() << '\n';
        return 1;
    }
}
