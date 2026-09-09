// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/shared-state.hpp"
#include "common/win32-handle.hpp"

#include <Windows.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace chatview {

class RuntimeController final {
public:
    RuntimeController() = default;
    ~RuntimeController();

    RuntimeController(const RuntimeController &) = delete;
    RuntimeController &operator=(const RuntimeController &) = delete;

    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;
    void update(bool streaming, bool recording) noexcept;
    [[nodiscard]] bool open_settings() const noexcept;

private:
    [[nodiscard]] bool create_transport_locked();
    [[nodiscard]] bool ensure_runtime_locked();
    [[nodiscard]] bool launch_runtime_locked();
    [[nodiscard]] std::wstring find_sibling_path(const wchar_t *file_name) const;

    void publish_locked(std::uint32_t flags) noexcept;
    void cleanup_locked() noexcept;

    std::mutex mutex_;
    UniqueHandle mapping_;
    UniqueHandle state_changed_event_;
    UniqueHandle runtime_process_;
    SharedState *shared_state_ = nullptr;
    std::wstring mapping_name_;
    std::wstring event_name_;
    std::uint64_t generation_ = 0U;
    std::uint32_t current_flags_ = SharedStateNone;
};

} // namespace chatview
