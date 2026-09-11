// SPDX-License-Identifier: GPL-2.0-or-later

#include "diagnostics/report.hpp"

#include <array>
#include <iostream>
#include <string>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    const std::array<chatview::DiagnosticEntry, 4U> entries = {{
        {L"status", L"YouTube chat ready"},
        {L"unsafe key", L"line one\nline two\tend"},
        {L"url", L"https://example.com/watch?v=private&token=secret"},
        {L"credentials", L"session=abc123 cookie=xyz authorization=BearerSecret"},
    }};

    const std::wstring report =
        chatview::build_diagnostics_report(entries);
    if (report.find(L"status=YouTube chat ready") == std::wstring::npos) {
        return fail("A safe diagnostics value was changed");
    }
    if (report.find(L"unsafe_key=line one line two end") ==
        std::wstring::npos) {
        return fail("Diagnostic controls or keys were not normalized");
    }
    if (report.find(L"[redacted-url]") == std::wstring::npos ||
        report.find(L"example.com") != std::wstring::npos ||
        report.find(L"private") != std::wstring::npos ||
        report.find(L"abc123") != std::wstring::npos ||
        report.find(L"BearerSecret") != std::wstring::npos) {
        return fail("Sensitive diagnostics material was not redacted");
    }
    if (report.find(L"session=[redacted]") == std::wstring::npos ||
        report.find(L"cookie=[redacted]") == std::wstring::npos ||
        report.find(L"authorization=[redacted]") ==
            std::wstring::npos) {
        return fail("Secret assignments were not marked as redacted");
    }
    if (report.find(L"No chat messages, raw URLs") ==
        std::wstring::npos) {
        return fail("The diagnostics privacy notice was omitted");
    }
    return 0;
}
