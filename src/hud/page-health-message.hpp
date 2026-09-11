// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/hud-health.hpp"

#include <string_view>

namespace chatview {

[[nodiscard]] const wchar_t *page_health_bootstrap_script() noexcept;

[[nodiscard]] HudProvider provider_for_chat_document(
    std::wstring_view url) noexcept;

[[nodiscard]] bool is_dom_reportable_page_state(
    HudPageState state) noexcept;

[[nodiscard]] bool parse_page_health_message(
    std::wstring_view message,
    HudHealthSnapshot &snapshot) noexcept;

} // namespace chatview
