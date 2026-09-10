// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/host-state-message.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const wchar_t *message)
{
    std::wcerr << message << L'\n';
    return 1;
}

} // namespace

int main()
{
    const std::wstring live = chatview::serialize_host_state_message(
        true, L"LIVE  •  REC", L"#ff3b30");
    if (live !=
        L"{\"type\":\"host-state\",\"editing\":true,"
        L"\"status\":\"LIVE  •  REC\",\"tone\":\"#ff3b30\"}") {
        return fail(L"A normal host-state message was serialized incorrectly");
    }

    std::wstring controls;
    controls.push_back(L'"');
    controls.push_back(L'\\');
    controls.push_back(L'\b');
    controls.push_back(L'\f');
    controls.push_back(L'\n');
    controls.push_back(L'\r');
    controls.push_back(L'\t');
    controls.push_back(static_cast<wchar_t>(1));

    const std::wstring escaped = chatview::serialize_host_state_message(
        false, controls, L"#aeb0b2");
    if (escaped !=
        L"{\"type\":\"host-state\",\"editing\":false,"
        L"\"status\":\"\\\"\\\\\\b\\f\\n\\r\\t\\u0001\","
        L"\"tone\":\"#aeb0b2\"}") {
        return fail(L"Host-state JSON escaping was incorrect");
    }

    const std::wstring unicode = chatview::serialize_host_state_message(
        false, L"채팅 준비됨", L"#5ac8fa");
    if (unicode.find(L"채팅 준비됨") == std::wstring::npos ||
        unicode.find(L"#5ac8fa") == std::wstring::npos) {
        return fail(L"Host-state serialization changed Unicode text");
    }

    if (chatview::serialize_host_state_message(false, L"", L"") !=
        L"{\"type\":\"host-state\",\"editing\":false,"
        L"\"status\":\"\",\"tone\":\"\"}") {
        return fail(L"Empty host-state values were serialized incorrectly");
    }

    return 0;
}
