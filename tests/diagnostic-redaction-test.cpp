// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/diagnostic-redaction.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

bool contains_case_sensitive(
    const std::wstring &text, const wchar_t *needle)
{
    return text.find(needle) != std::wstring::npos;
}

} // namespace

int main()
{
    const chatview::DiagnosticRedactionContext context{
        L"C:\\Users\\Alice",
        L"C:\\Users\\Alice\\AppData\\Local",
        L"C:\\Users\\Alice\\AppData\\Roaming",
    };

    const std::wstring input =
        L"[ChatView OBS] opening https://www.youtube.com/watch?v=SecretVideo "
        L"from C:\\Users\\Alice\\AppData\\Local\\ChatView\\chat.ini\n"
        L"--mapping \"Local\\ChatViewOBS.State.123.0123456789abcdef\" "
        L"--status-event=Local\\ChatViewOBS.ControlChanged.123.deadbeef "
        L"object Local\\ChatViewOBS.Ready.123.feedface "
        L"roaming C:\\Users\\Alice\\AppData\\Roaming\\obs-studio\x01";

    const std::wstring redacted =
        chatview::redact_diagnostic_text(input, context);

    if (contains_case_sensitive(redacted, L"SecretVideo") ||
        contains_case_sensitive(redacted, L"C:\\Users\\Alice") ||
        contains_case_sensitive(redacted, L"0123456789abcdef") ||
        contains_case_sensitive(redacted, L"deadbeef") ||
        contains_case_sensitive(redacted, L"feedface") ||
        contains_case_sensitive(redacted, L"\x01")) {
        return fail("A private diagnostic value survived redaction");
    }

    if (!contains_case_sensitive(redacted, L"<url-redacted>") ||
        !contains_case_sensitive(redacted, L"%LOCALAPPDATA%") ||
        !contains_case_sensitive(redacted, L"%APPDATA%") ||
        !contains_case_sensitive(redacted, L"--mapping \"<redacted>\"") ||
        !contains_case_sensitive(
            redacted, L"--status-event=<redacted>") ||
        !contains_case_sensitive(
            redacted, L"Local\\ChatViewOBS.<redacted>") ||
        !contains_case_sensitive(redacted, L"[ChatView OBS]")) {
        return fail("Expected diagnostic structure was lost during redaction");
    }

    const std::wstring mixed_case =
        chatview::redact_diagnostic_text(
            L"HTTPS://EXAMPLE.COM/private "
            L"c:\\users\\alice\\appdata\\local\\ChatView",
            context);
    if (contains_case_sensitive(mixed_case, L"EXAMPLE.COM") ||
        contains_case_sensitive(mixed_case, L"users\\alice") ||
        !contains_case_sensitive(mixed_case, L"<url-redacted>") ||
        !contains_case_sensitive(mixed_case, L"%LOCALAPPDATA%")) {
        return fail("Case-insensitive redaction failed");
    }

    const std::wstring unchanged =
        chatview::redact_diagnostic_text(
            L"[ChatView OBS] HUD runtime started", {});
    if (unchanged != L"[ChatView OBS] HUD runtime started") {
        return fail("Safe diagnostic text changed unexpectedly");
    }

    return 0;
}
