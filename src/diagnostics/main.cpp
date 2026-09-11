// SPDX-License-Identifier: GPL-2.0-or-later

#include "diagnostics/diagnostics-exporter.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const chatview::DiagnosticsExportResult result =
        chatview::export_diagnostics_bundle();
    if (!result.success) {
        const std::wstring message =
            result.error.empty()
                ? L"ChatView diagnostics could not be exported."
                : result.error;
        MessageBoxW(
            nullptr,
            message.c_str(),
            L"ChatView Diagnostics",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    const std::wstring message =
        L"A privacy-filtered diagnostics folder was created on the Desktop.\n\n" +
        result.directory.wstring() +
        L"\n\nReview the files before sharing them.";
    MessageBoxW(
        nullptr,
        message.c_str(),
        L"ChatView Diagnostics",
        MB_OK | MB_ICONINFORMATION);

    const HINSTANCE opened = ShellExecuteW(
        nullptr,
        L"open",
        result.directory.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(opened) > 32 ? 0 : 2;
}
