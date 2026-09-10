// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/transport-token.hpp"

#include <iostream>
#include <set>
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
    if (chatview::is_valid_transport_token(L"") ||
        chatview::is_valid_transport_token(L"0123456789abcdef") ||
        chatview::is_valid_transport_token(
            L"0123456789abcdef0123456789abcde") ||
        chatview::is_valid_transport_token(
            L"0123456789abcdef0123456789abcdef0") ||
        chatview::is_valid_transport_token(
            L"0123456789abcdef0123456789abcdeg") ||
        chatview::is_valid_transport_token(
            L"0123456789abcdef0123456789abcdeF")) {
        return fail(L"Transport token validation accepted malformed input");
    }

    std::set<std::wstring> tokens;
    for (unsigned int attempt = 0U; attempt < 256U; ++attempt) {
        std::wstring token = chatview::create_transport_token();
        if (!chatview::is_valid_transport_token(token)) {
            return fail(L"CNG did not produce a valid transport token");
        }
        if (!tokens.insert(std::move(token)).second) {
            return fail(L"CNG produced a duplicate transport token");
        }
    }

    return 0;
}
