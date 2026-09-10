// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace chatview {

[[nodiscard]] std::wstring serialize_host_state_message(
    bool editing,
    std::wstring_view status_text,
    std::wstring_view status_tone) noexcept;

} // namespace chatview
