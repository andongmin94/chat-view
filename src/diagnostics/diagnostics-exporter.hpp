// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace chatview {

struct DiagnosticsExportResult {
    bool success = false;
    std::filesystem::path directory;
    std::wstring error;
};

[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle() noexcept;
[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root) noexcept;

} // namespace chatview
