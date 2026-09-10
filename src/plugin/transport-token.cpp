// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/transport-token.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstddef>
#include <string>

namespace chatview {
namespace {

constexpr std::size_t kTransportTokenByteLength =
    kTransportTokenHexLength / 2U;
constexpr wchar_t kHexDigits[] = L"0123456789abcdef";

static_assert(kTransportTokenHexLength % 2U == 0U);

} // namespace

std::wstring create_transport_token() noexcept
{
    std::array<unsigned char, kTransportTokenByteLength> bytes{};
    const NTSTATUS status = BCryptGenRandom(
        nullptr,
        bytes.data(),
        static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        return {};
    }

    std::wstring token(kTransportTokenHexLength, L'0');
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const unsigned char value = bytes[index];
        token[index * 2U] = kHexDigits[value >> 4U];
        token[index * 2U + 1U] = kHexDigits[value & 0x0FU];
    }
    return token;
}

bool is_valid_transport_token(std::wstring_view token) noexcept
{
    if (token.size() != kTransportTokenHexLength) {
        return false;
    }

    for (const wchar_t character : token) {
        const bool digit = character >= L'0' && character <= L'9';
        const bool lower_hex = character >= L'a' && character <= L'f';
        if (!digit && !lower_hex) {
            return false;
        }
    }
    return true;
}

} // namespace chatview
