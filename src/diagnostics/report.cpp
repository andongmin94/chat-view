// SPDX-License-Identifier: GPL-2.0-or-later

#include "diagnostics/report.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace chatview {
namespace {

constexpr std::size_t kMaximumDiagnosticValueLength = 512U;
constexpr std::size_t kMaximumDiagnosticKeyLength = 64U;
constexpr std::wstring_view kRedactedUrl = L"[redacted-url]";
constexpr std::wstring_view kRedactedSecret = L"[redacted]";

wchar_t lower_character(wchar_t character) noexcept
{
    return static_cast<wchar_t>(std::towlower(character));
}

bool starts_with_case_insensitive(
    std::wstring_view value,
    std::size_t offset,
    std::wstring_view pattern) noexcept
{
    if (offset > value.size() || pattern.size() > value.size() - offset) {
        return false;
    }
    for (std::size_t index = 0U; index < pattern.size(); ++index) {
        if (lower_character(value[offset + index]) !=
            lower_character(pattern[index])) {
            return false;
        }
    }
    return true;
}

bool is_secret_delimiter(wchar_t character) noexcept
{
    return std::iswspace(character) != 0 || character == L'&' ||
           character == L';' || character == L',';
}

std::wstring sanitize_key(std::wstring_view key)
{
    std::wstring sanitized;
    sanitized.reserve(std::min(key.size(), kMaximumDiagnosticKeyLength));
    for (const wchar_t character : key) {
        if (sanitized.size() >= kMaximumDiagnosticKeyLength) {
            break;
        }
        const bool allowed =
            (character >= L'a' && character <= L'z') ||
            (character >= L'A' && character <= L'Z') ||
            (character >= L'0' && character <= L'9') ||
            character == L'_' || character == L'-' || character == L'.';
        sanitized.push_back(allowed ? character : L'_');
    }
    return sanitized.empty() ? L"unnamed" : sanitized;
}

void redact_secret_assignments(std::wstring &value)
{
    constexpr std::array<std::wstring_view, 10U> keys = {
        L"access_token=",
        L"refresh_token=",
        L"authorization=",
        L"password=",
        L"passwd=",
        L"cookie=",
        L"session=",
        L"token=",
        L"secret=",
        L"api_key=",
    };

    std::size_t offset = 0U;
    while (offset < value.size()) {
        std::wstring_view matched;
        for (const std::wstring_view key : keys) {
            if (starts_with_case_insensitive(value, offset, key)) {
                matched = key;
                break;
            }
        }
        if (matched.empty()) {
            ++offset;
            continue;
        }

        const std::size_t secret_begin = offset + matched.size();
        std::size_t secret_end = secret_begin;
        while (secret_end < value.size() &&
               !is_secret_delimiter(value[secret_end])) {
            ++secret_end;
        }
        value.replace(
            secret_begin,
            secret_end - secret_begin,
            kRedactedSecret);
        offset = secret_begin + kRedactedSecret.size();
    }
}

} // namespace

std::wstring sanitize_diagnostic_value(std::wstring_view value)
{
    std::wstring sanitized;
    sanitized.reserve(std::min(value.size(), kMaximumDiagnosticValueLength));

    std::size_t index = 0U;
    while (index < value.size() &&
           sanitized.size() < kMaximumDiagnosticValueLength) {
        if (starts_with_case_insensitive(value, index, L"https://") ||
            starts_with_case_insensitive(value, index, L"http://")) {
            sanitized.append(kRedactedUrl);
            while (index < value.size() &&
                   std::iswspace(value[index]) == 0) {
                ++index;
            }
            continue;
        }

        const wchar_t character = value[index++];
        if (character == L'\r' || character == L'\n' ||
            character == L'\t' || character < 0x20) {
            if (sanitized.empty() || sanitized.back() != L' ') {
                sanitized.push_back(L' ');
            }
        } else {
            sanitized.push_back(character);
        }
    }

    while (!sanitized.empty() && sanitized.back() == L' ') {
        sanitized.pop_back();
    }
    redact_secret_assignments(sanitized);
    return sanitized;
}

std::wstring build_diagnostics_report(
    std::span<const DiagnosticEntry> entries)
{
    std::wstring report =
        L"ChatView diagnostics\r\n"
        L"format_version=1\r\n"
        L"privacy_notice=No chat messages, raw URLs, cookies, tokens, or "
        L"browser profile data are collected.\r\n";

    for (const DiagnosticEntry &entry : entries) {
        report.append(sanitize_key(entry.key));
        report.push_back(L'=');
        report.append(sanitize_diagnostic_value(entry.value));
        report.append(L"\r\n");
    }
    return report;
}

} // namespace chatview
