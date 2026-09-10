// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/host-state-message.hpp"

#include <string>
#include <string_view>

namespace chatview {
namespace {

constexpr wchar_t kHexDigits[] = L"0123456789abcdef";

void append_json_string(std::wstring &output, std::wstring_view value)
{
    output.push_back(L'"');
    for (const wchar_t character : value) {
        switch (character) {
        case L'"':
            output.append(L"\\\"");
            break;
        case L'\\':
            output.append(L"\\\\");
            break;
        case L'\b':
            output.append(L"\\b");
            break;
        case L'\f':
            output.append(L"\\f");
            break;
        case L'\n':
            output.append(L"\\n");
            break;
        case L'\r':
            output.append(L"\\r");
            break;
        case L'\t':
            output.append(L"\\t");
            break;
        default: {
            const unsigned int value_code =
                static_cast<unsigned int>(character);
            if (value_code < 0x20U) {
                output.append(L"\\u00");
                output.push_back(kHexDigits[(value_code >> 4U) & 0x0FU]);
                output.push_back(kHexDigits[value_code & 0x0FU]);
            } else {
                output.push_back(character);
            }
            break;
        }
        }
    }
    output.push_back(L'"');
}

} // namespace

std::wstring serialize_host_state_message(
    bool editing,
    std::wstring_view status_text,
    std::wstring_view status_tone) noexcept
{
    try {
        std::wstring output;
        output.reserve(status_text.size() + status_tone.size() + 80U);
        output.append(L"{\"type\":\"host-state\",\"editing\":");
        output.append(editing ? L"true" : L"false");
        output.append(L",\"status\":");
        append_json_string(output, status_text);
        output.append(L",\"tone\":");
        append_json_string(output, status_tone);
        output.push_back(L'}');
        return output;
    } catch (...) {
        return {};
    }
}

} // namespace chatview
