// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace chatview {

inline constexpr std::size_t kTransportTokenHexLength = 32U;

[[nodiscard]] std::wstring create_transport_token() noexcept;
[[nodiscard]] bool is_valid_transport_token(std::wstring_view token) noexcept;

} // namespace chatview
