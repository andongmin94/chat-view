// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/saved-connection.hpp"
#include "common/win32-handle.hpp"
#include <Windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <algorithm>
#include <filesystem>
#include <string_view>
#include <vector>

namespace chatview {
namespace {
constexpr DWORD kMaximumBytes = 16384;
struct Blob {
    DATA_BLOB value{};
    ~Blob() { if (value.pbData) { SecureZeroMemory(value.pbData, value.cbData); LocalFree(value.pbData); } }
};
struct Wipe {
    std::wstring &value;
    ~Wipe() { if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); }
};
std::filesystem::path path()
{
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size < 2 || size > 32768) throw 1;
    std::wstring root(size, L'\0');
    const DWORD read = GetEnvironmentVariableW(L"LOCALAPPDATA", root.data(), size);
    if (!read || read >= size) throw 1;
    root.resize(read);
    return std::filesystem::path(root) / L"ChatView" / L"saved-connection.bin";
}
bool valid(const SavedConnection &value)
{
    return !value.origin.empty() && value.origin.size() <= 2048 &&
        std::all_of(value.origin.begin(), value.origin.end(), [](wchar_t c) { return c > 32 && c < 127; }) &&
        value.credential.size() == 64 &&
        std::all_of(value.credential.begin(), value.credential.end(), [](wchar_t c) {
            return (c >= L'a' && c <= L'f') || (c >= L'0' && c <= L'9');
        });
}
}
bool save_connection(const SavedConnection &connection) noexcept
{
    try {
        if (!valid(connection)) return false;
        std::wstring plain = L"1\n" + connection.origin + L"\n" + connection.credential +
                             (connection.developer_loopback ? L"\n1" : L"\n0");
        Wipe wipe{plain};
        DATA_BLOB input{static_cast<DWORD>(plain.size() * sizeof(wchar_t)), reinterpret_cast<BYTE *>(plain.data())};
        Blob encrypted;
        if (!CryptProtectData(&input, L"ChatView remembered connection", nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &encrypted.value)) return false;
        const auto destination = path();
        std::filesystem::create_directories(destination.parent_path());
        const auto temporary = destination.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        const HANDLE raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
        if (raw == INVALID_HANDLE_VALUE) return false;
        UniqueHandle file(raw);
        DWORD written = 0;
        const bool complete = WriteFile(file.get(), encrypted.value.pbData, encrypted.value.cbData, &written, nullptr) &&
                              written == encrypted.value.cbData && FlushFileBuffers(file.get());
        file.reset();
        if (!complete || !MoveFileExW(temporary.c_str(), destination.c_str(),
                                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str()); return false;
        }
        return true;
    } catch (...) { return false; }
}
std::optional<SavedConnection> load_connection() noexcept
{
    try {
        const HANDLE raw = CreateFileW(path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (raw == INVALID_HANDLE_VALUE) return std::nullopt;
        UniqueHandle file(raw);
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(file.get(), &length) || length.QuadPart <= 0 || length.QuadPart > kMaximumBytes) return std::nullopt;
        std::vector<BYTE> bytes(static_cast<size_t>(length.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()) return std::nullopt;
        DATA_BLOB input{read, bytes.data()}; Blob plain;
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain.value) ||
            plain.value.cbData % sizeof(wchar_t) || plain.value.cbData > kMaximumBytes) return std::nullopt;
        const std::wstring_view text(reinterpret_cast<wchar_t *>(plain.value.pbData), plain.value.cbData / sizeof(wchar_t));
        const auto first = text.find(L'\n', 2), second = text.find(L'\n', first == text.npos ? text.size() : first + 1);
        if (!text.starts_with(L"1\n") || first == text.npos || second == text.npos ||
            second + 2 != text.size() || (text.back() != L'0' && text.back() != L'1')) return std::nullopt;
        SavedConnection value{std::wstring(text.substr(2, first - 2)),
                                 std::wstring(text.substr(first + 1, second - first - 1)), text.back() == L'1'};
        if (!valid(value)) return std::nullopt;
        return value;
    } catch (...) { return std::nullopt; }
}
bool forget_connection() noexcept
{
    try {
        if (DeleteFileW(path().c_str())) return true;
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    } catch (...) { return false; }
}
void forget_matching_connection(const std::wstring &credential) noexcept
{
    auto saved = load_connection();
    if (!saved) return;
    if (saved->credential == credential) (void)forget_connection();
    if (!saved->credential.empty()) SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
}
}
