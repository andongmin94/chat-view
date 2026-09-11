// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace chatview {

struct DiagnosticRedactionContext {
    std::wstring user_profile;
    std::wstring local_app_data;
    std::wstring roaming_app_data;
};

[[nodiscard]] std::wstring redact_diagnostic_text(
    std::wstring_view text,
    const DiagnosticRedactionContext &context);

} // namespace chatview
