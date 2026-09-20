// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <optional>
#include <string>

namespace chatview {
struct SavedConnection {
    std::wstring origin;
    std::wstring credential;
    bool developer_loopback = false;
};
// User-scoped Windows DPAPI, never plaintext INI or provider credentials.
[[nodiscard]] std::optional<SavedConnection> load_connection() noexcept;
[[nodiscard]] bool save_connection(const SavedConnection &connection) noexcept;
[[nodiscard]] bool forget_connection() noexcept;
void forget_matching_connection(const std::wstring &credential) noexcept;
}
