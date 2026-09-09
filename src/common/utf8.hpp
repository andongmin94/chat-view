// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace chatview {

[[nodiscard]] inline std::wstring utf8_to_wide(std::string_view text)
{
    if (text.empty()) {
        return {};
    }

    const int input_length = static_cast<int>(text.size());
    int output_length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        input_length,
        nullptr,
        0);
    DWORD flags = MB_ERR_INVALID_CHARS;

    if (output_length == 0) {
        flags = 0U;
        output_length = MultiByteToWideChar(
            CP_UTF8,
            flags,
            text.data(),
            input_length,
            nullptr,
            0);
    }

    if (output_length <= 0) {
        return {};
    }

    std::wstring converted(static_cast<std::size_t>(output_length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            flags,
            text.data(),
            input_length,
            converted.data(),
            output_length) != output_length) {
        return {};
    }

    return converted;
}

[[nodiscard]] inline std::string wide_to_utf8(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }

    const int input_length = static_cast<int>(text.size());
    const int output_length = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        input_length,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (output_length <= 0) {
        return {};
    }

    std::string converted(static_cast<std::size_t>(output_length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            input_length,
            converted.data(),
            output_length,
            nullptr,
            nullptr) != output_length) {
        return {};
    }

    return converted;
}

[[nodiscard]] inline std::string truncate_utf8(std::string_view text, std::size_t maximum_bytes)
{
    if (text.size() <= maximum_bytes) {
        return std::string(text);
    }

    std::size_t length = maximum_bytes;
    while (length > 0U &&
           (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U) {
        --length;
    }

    return std::string(text.substr(0U, length));
}

} // namespace chatview
