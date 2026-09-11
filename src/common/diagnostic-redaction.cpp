// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/diagnostic-redaction.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace chatview {
namespace {

wchar_t lowercase_ascii(wchar_t character) noexcept
{
    if (character >= L'A' && character <= L'Z') {
        return static_cast<wchar_t>(character - L'A' + L'a');
    }
    return character;
}

bool matches_at_case_insensitive(
    std::wstring_view text,
    std::size_t position,
    std::wstring_view pattern) noexcept
{
    if (position > text.size() || pattern.size() > text.size() - position) {
        return false;
    }

    for (std::size_t index = 0U; index < pattern.size(); ++index) {
        if (lowercase_ascii(text[position + index]) !=
            lowercase_ascii(pattern[index])) {
            return false;
        }
    }
    return true;
}

std::size_t find_case_insensitive(
    std::wstring_view text,
    std::wstring_view pattern,
    std::size_t start = 0U) noexcept
{
    if (pattern.empty() || start > text.size()) {
        return std::wstring_view::npos;
    }

    for (std::size_t position = start;
         position + pattern.size() <= text.size();
         ++position) {
        if (matches_at_case_insensitive(text, position, pattern)) {
            return position;
        }
    }
    return std::wstring_view::npos;
}

void replace_all_case_insensitive(
    std::wstring &text,
    std::wstring_view pattern,
    std::wstring_view replacement)
{
    if (pattern.empty()) {
        return;
    }

    std::size_t position = 0U;
    while ((position = find_case_insensitive(text, pattern, position)) !=
           std::wstring_view::npos) {
        text.replace(position, pattern.size(), replacement);
        position += replacement.size();
    }
}

bool is_value_terminator(wchar_t character) noexcept
{
    return std::iswspace(character) != 0 || character == L'\'' ||
           character == L'"' || character == L'<' || character == L'>' ||
           character == L')' || character == L']' || character == L'}' ||
           character == L',' || character == L';';
}

void redact_urls(std::wstring &text)
{
    constexpr std::array<std::wstring_view, 2U> schemes{
        L"https://",
        L"http://",
    };
    constexpr std::wstring_view replacement = L"<url-redacted>";

    std::size_t search_position = 0U;
    while (search_position < text.size()) {
        std::size_t match = std::wstring_view::npos;
        for (const std::wstring_view scheme : schemes) {
            const std::size_t candidate =
                find_case_insensitive(text, scheme, search_position);
            if (candidate != std::wstring_view::npos &&
                (match == std::wstring_view::npos || candidate < match)) {
                match = candidate;
            }
        }
        if (match == std::wstring_view::npos) {
            return;
        }

        std::size_t end = match;
        while (end < text.size() && !is_value_terminator(text[end])) {
            ++end;
        }
        text.replace(match, end - match, replacement);
        search_position = match + replacement.size();
    }
}

void redact_named_objects(std::wstring &text)
{
    constexpr std::wstring_view prefix = L"Local\\ChatViewOBS.";
    constexpr std::wstring_view replacement =
        L"Local\\ChatViewOBS.<redacted>";

    std::size_t search_position = 0U;
    while (search_position < text.size()) {
        const std::size_t match =
            find_case_insensitive(text, prefix, search_position);
        if (match == std::wstring_view::npos) {
            return;
        }

        std::size_t end = match + prefix.size();
        while (end < text.size() && !is_value_terminator(text[end])) {
            ++end;
        }
        text.replace(match, end - match, replacement);
        search_position = match + replacement.size();
    }
}

bool is_argument_boundary(
    std::wstring_view text, std::size_t position) noexcept
{
    return position == 0U || std::iswspace(text[position - 1U]) != 0;
}

void redact_argument_value(
    std::wstring &text, std::wstring_view argument)
{
    constexpr std::wstring_view replacement = L"<redacted>";
    std::size_t search_position = 0U;

    while (search_position < text.size()) {
        const std::size_t match =
            find_case_insensitive(text, argument, search_position);
        if (match == std::wstring_view::npos) {
            return;
        }
        if (!is_argument_boundary(text, match)) {
            search_position = match + argument.size();
            continue;
        }

        std::size_t value_start = match + argument.size();
        while (value_start < text.size() &&
               std::iswspace(text[value_start]) != 0) {
            ++value_start;
        }
        if (value_start < text.size() && text[value_start] == L'=') {
            ++value_start;
            while (value_start < text.size() &&
                   std::iswspace(text[value_start]) != 0) {
                ++value_start;
            }
        }
        if (value_start >= text.size()) {
            return;
        }

        std::size_t value_end = value_start;
        if (text[value_start] == L'"' || text[value_start] == L'\'') {
            const wchar_t quote = text[value_start];
            ++value_start;
            value_end = value_start;
            while (value_end < text.size() && text[value_end] != quote) {
                ++value_end;
            }
        } else {
            while (value_end < text.size() &&
                   std::iswspace(text[value_end]) == 0) {
                ++value_end;
            }
        }

        text.replace(value_start, value_end - value_start, replacement);
        search_position = value_start + replacement.size();
    }
}

void scrub_control_characters(std::wstring &text) noexcept
{
    for (wchar_t &character : text) {
        if (character < 0x20 && character != L'\r' &&
            character != L'\n' && character != L'\t') {
            character = L' ';
        }
    }
}

} // namespace

std::wstring redact_diagnostic_text(
    std::wstring_view text,
    const DiagnosticRedactionContext &context)
{
    std::wstring redacted(text);

    const std::array<std::pair<std::wstring_view, std::wstring_view>, 3U>
        paths{{
            {context.local_app_data, L"%LOCALAPPDATA%"},
            {context.roaming_app_data, L"%APPDATA%"},
            {context.user_profile, L"%USERPROFILE%"},
        }};
    for (const auto &[path, replacement] : paths) {
        if (!path.empty()) {
            replace_all_case_insensitive(redacted, path, replacement);
        }
    }

    constexpr std::array<std::wstring_view, 6U> private_arguments{
        L"--mapping",
        L"--event",
        L"--ready-event",
        L"--status-mapping",
        L"--status-event",
        L"--restart-event",
    };
    for (const std::wstring_view argument : private_arguments) {
        redact_argument_value(redacted, argument);
    }

    redact_named_objects(redacted);
    redact_urls(redacted);
    scrub_control_characters(redacted);
    return redacted;
}

} // namespace chatview
