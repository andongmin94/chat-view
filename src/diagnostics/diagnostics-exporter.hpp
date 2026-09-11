// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>

#define CHATVIEW_WIDEN_INNER(value) L##value
#define CHATVIEW_WIDEN(value) CHATVIEW_WIDEN_INNER(value)

namespace chatview {

struct DiagnosticsExportResult {
    bool success = false;
    std::filesystem::path directory;
    std::wstring error;
};

[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle() noexcept;

} // namespace chatview
