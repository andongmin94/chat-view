// SPDX-License-Identifier: GPL-2.0-or-later

#include "chat/twitch-config.hpp"

#include "common/utf8.hpp"

#include <Windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace chatview {
namespace {

constexpr wchar_t kSection[] = L"twitch";
constexpr wchar_t kEnabledKey[] = L"enabled";
constexpr wchar_t kChannelKey[] = L"channel";
constexpr wchar_t kUsernameKey[] = L"username";
constexpr wchar_t kProtectedTokenKey[] = L"token_dpapi";

std::filesystem::path config_file_path()
{
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0U);
    if (required == 0U) {
        return {};
    }

    std::wstring local_app_data(required, L'\0');
    const DWORD written =
        GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(), required);
    if (written == 0U || written >= required) {
        return {};
    }
    local_app_data.resize(written);

    return std::filesystem::path(local_app_data) / L"ChatView" / L"twitch.ini";
}

std::wstring read_profile_value(const std::filesystem::path &path, const wchar_t *key)
{
    std::array<wchar_t, 4096U> buffer{};
    const DWORD length = GetPrivateProfileStringW(
        kSection,
        key,
        L"",
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        path.c_str());
    if (length == 0U || length >= buffer.size() - 1U) {
        return {};
    }
    return std::wstring(buffer.data(), length);
}

std::string normalize_identifier(std::string value, bool allow_hash_prefix)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }

    if (allow_hash_prefix && !value.empty() && value.front() == '#') {
        value.erase(value.begin());
    }

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (value.empty() || value.size() > 25U) {
        return {};
    }

    const bool valid = std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_';
    });
    return valid ? value : std::string();
}

std::optional<std::string> unprotect_token(std::wstring_view encoded)
{
    if (encoded.empty()) {
        return std::string();
    }

    DWORD protected_size = 0U;
    if (!CryptStringToBinaryW(
            encoded.data(),
            static_cast<DWORD>(encoded.size()),
            CRYPT_STRING_BASE64,
            nullptr,
            &protected_size,
            nullptr,
            nullptr) ||
        protected_size == 0U) {
        return std::nullopt;
    }

    std::vector<BYTE> protected_bytes(protected_size);
    if (!CryptStringToBinaryW(
            encoded.data(),
            static_cast<DWORD>(encoded.size()),
            CRYPT_STRING_BASE64,
            protected_bytes.data(),
            &protected_size,
            nullptr,
            nullptr)) {
        return std::nullopt;
    }
    protected_bytes.resize(protected_size);

    DATA_BLOB input{
        static_cast<DWORD>(protected_bytes.size()),
        protected_bytes.data(),
    };
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0U, &output)) {
        SecureZeroMemory(protected_bytes.data(), protected_bytes.size());
        return std::nullopt;
    }

    std::string token(
        reinterpret_cast<const char *>(output.pbData),
        static_cast<std::size_t>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    SecureZeroMemory(protected_bytes.data(), protected_bytes.size());

    if (token.rfind("oauth:", 0U) == 0U) {
        token.erase(0U, 6U);
    }

    const bool invalid = token.empty() ||
                         std::any_of(token.begin(), token.end(), [](unsigned char character) {
                             return character == '\r' || character == '\n' || character == '\0' ||
                                    std::isspace(character) != 0;
                         });
    if (invalid) {
        SecureZeroMemory(token.data(), token.size());
        return std::nullopt;
    }

    return token;
}

} // namespace

std::optional<TwitchConfig> load_twitch_config(std::wstring &error) noexcept
{
    error.clear();

    try {
        const std::filesystem::path path = config_file_path();
        if (path.empty() || !std::filesystem::is_regular_file(path)) {
            return std::nullopt;
        }

        if (GetPrivateProfileIntW(kSection, kEnabledKey, 0, path.c_str()) == 0U) {
            return std::nullopt;
        }

        TwitchConfig config;
        config.channel = normalize_identifier(
            wide_to_utf8(read_profile_value(path, kChannelKey)),
            true);
        if (config.channel.empty()) {
            error = L"Twitch configuration has an invalid channel name.";
            return std::nullopt;
        }

        const std::wstring username_value = read_profile_value(path, kUsernameKey);
        const std::wstring token_value = read_profile_value(path, kProtectedTokenKey);
        if (username_value.empty() && token_value.empty()) {
            return config;
        }
        if (username_value.empty() || token_value.empty()) {
            error = L"Twitch username and token must be configured together.";
            return std::nullopt;
        }

        config.username = normalize_identifier(wide_to_utf8(username_value), false);
        if (config.username.empty()) {
            error = L"Twitch configuration has an invalid username.";
            return std::nullopt;
        }

        std::optional<std::string> token = unprotect_token(token_value);
        if (!token.has_value()) {
            error = L"Twitch token could not be decrypted for this Windows user.";
            return std::nullopt;
        }
        config.oauth_token = std::move(*token);
        return config;
    } catch (...) {
        error = L"Twitch configuration could not be read.";
        return std::nullopt;
    }
}

} // namespace chatview
