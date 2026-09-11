// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string>
#include <string_view>

namespace chatview {

struct DiagnosticEntry {
    std::wstring key;
    std::wstring value;
};

[[nodiscard]] std::wstring sanitize_diagnostic_value(
    std::wstring_view value);
[[nodiscard]] std::wstring build_diagnostics_report(
    std::span<const DiagnosticEntry> entries);

} // namespace chatview
