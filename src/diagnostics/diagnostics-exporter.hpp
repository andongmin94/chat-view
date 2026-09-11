// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/control-status.hpp"
#include "common/hud-health.hpp"

#include <filesystem>
#include <string>

namespace chatview {

struct DiagnosticsRuntimeSnapshot {
    bool obs_connected = false;
    bool control_status_available = false;
    ControlStatusSnapshot control_status;
    bool hud_health_available = false;
    HudHealthSnapshot hud_health;
};

struct DiagnosticsExportResult {
    bool success = false;
    std::filesystem::path directory;
    std::wstring error;
};

[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle() noexcept;
[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle(
    const DiagnosticsRuntimeSnapshot &runtime) noexcept;
[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root) noexcept;
[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root,
    const DiagnosticsRuntimeSnapshot &runtime) noexcept;

} // namespace chatview
