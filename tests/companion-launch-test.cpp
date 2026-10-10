// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual shipping entry point, not a fake OBS parent or HudWindow alone.
#include "common/win32-handle.hpp"
#include <Windows.h>
#include <TlHelp32.h>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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
void expect_video_scope(HWND panel)
{
    const HWND scope = GetDlgItem(panel, 111);
    wchar_t value[256]{};
    RECT bounds{}, client{};
    expect(scope && IsWindowVisible(scope) && GetWindowRect(scope, &bounds) && GetClientRect(panel, &client),
        "persistent video notice is a visible native control");
    MapWindowPoints(nullptr, panel, reinterpret_cast<POINT *>(&bounds), 2);
    GetWindowTextW(scope, value, 256);
    expect(std::wstring(value) == L"수신 영상의 HUD 제외: 미검증\n채팅 연결·OBS 활성 보고는 영상 검증이 아닙니다.",
        "companion separates chat/OBS reports from video verification");
    expect(bounds.left >= 0 && bounds.top >= 0 && bounds.right <= client.right && bounds.bottom <= client.bottom,
        "video notice remains inside the connection panel client area");
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
        expect_video_scope(panel);
        choose(panel, 104); // Empty service address fails locally, not a live login.
        expect_video_scope(panel);
        choose(panel, 105);
        expect_video_scope(panel);
        const HWND hud = await_window(child, L"ChatView HUD");
        expect(PostThreadMessageW(child.thread_id, WM_HOTKEY, 0x4356U,
            MAKELPARAM(MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'V')) != FALSE, "open companion video controls");
        const HWND video = await_window(child, L"ChatView · 게임 창 별도 출력 (실험)");
        expect(GetDlgItem(video, 201) && GetDlgItem(video, 202) && GetDlgItem(video, 204) &&
            GetDlgItem(video, 205) && GetDlgItem(video, 209),
            "window/video controls and the native chat entry share the OBS-free companion");
        // The entry must reuse the existing protected native panel without
        // switching roles, starting login, selecting a screen, or starting WGC.
        DWORD initial_affinity = 0;
        expect(GetWindowDisplayAffinity(panel, &initial_affinity) &&
            initial_affinity == WDA_EXCLUDEFROMCAPTURE, "native chat panel starts protected");
        DWORD_PTR ignored = 0;
        expect(SendMessageTimeoutW(panel, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000U, &ignored) != 0 &&
            !IsWindowVisible(panel), "hide the existing native chat panel without logging out");
        choose(video, 209);
        expect(await_window(child, kPanelTitle) == panel,
            "video panel opens the same protected native chat window without new enrollment");
        DWORD reopened_affinity = 0;
        expect(GetWindowDisplayAffinity(panel, &reopened_affinity) &&
            reopened_affinity == WDA_EXCLUDEFROMCAPTURE, "video-to-chat action preserves capture exclusion");
        wchar_t role[256]{};
        GetDlgItemTextW(panel, 109, role, 256);
        expect(std::wstring_view(role).find(L"게임 PC") != std::wstring_view::npos,
            "companion chat keeps the gaming role and never starts OBS");
        expect_video_scope(panel);
        expect(SendDlgItemMessageW(video, 201, CB_GETCURSEL, 0, 0) == CB_ERR &&
            SendDlgItemMessageW(video, 202, CB_GETCURSEL, 0, 0) == CB_ERR, "capture targets are never preselected");
        choose(video, 204);
        WindowQuery unexpected{child.pid, L"ChatView · 창 영상 출력"};
        EnumWindows(find_window, reinterpret_cast<LPARAM>(&unexpected));
        expect(!unexpected.found, "missing selection does not open an output or capture desktop");
        choose(video, 205);
        expect(IsWindowVisible(hud) != FALSE && IsWindowVisible(panel) != FALSE,
            "video stop preserves visible local HUD and existing native chat panel");
        expect_video_scope(panel);
        expect_no_obs_module(child.pid);
        {
            Child duplicate(argv[1]);
            duplicate.expect_exit(13U);
        }
        expect(PostThreadMessageW(child.thread_id, WM_HOTKEY, 0x4351U,
            MAKELPARAM(MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'Q')) != FALSE,
            "request normal companion shutdown");
        child.expect_exit(0U);
        std::cout << "Companion entry, consent, chat/video selection UI, scoped status, no OBS modules, duplicate and exit passed\n";
    } catch (const std::exception &error) {
        std::cerr << "Companion test failed: " << error.what() << '\n';
        return 1;
    }
}
