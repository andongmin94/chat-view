// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/runtime-telemetry.hpp"

#include <filesystem>

namespace chatview {

class RuntimeHistoryStore final {
public:
    RuntimeHistoryStore() noexcept;
    explicit RuntimeHistoryStore(
        const std::filesystem::path &directory) noexcept;

    [[nodiscard]] bool load(
        RuntimeTelemetrySnapshot &snapshot) const noexcept;
    [[nodiscard]] bool save(
        const RuntimeTelemetrySnapshot &snapshot) const noexcept;

    [[nodiscard]] const std::filesystem::path &file_path() const noexcept;

private:
    std::filesystem::path file_path_;
};

} // namespace chatview
