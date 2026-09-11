// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/control-status.hpp"
#include "common/srw-lock.hpp"
#include "common/win32-handle.hpp"

#include <Windows.h>

#include <cstdint>
#include <string>

namespace chatview {

class ControlCenterBridge final {
public:
    ControlCenterBridge() = default;
    ~ControlCenterBridge();

    ControlCenterBridge(const ControlCenterBridge &) = delete;
    ControlCenterBridge &operator=(const ControlCenterBridge &) = delete;

    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;

    void update(
        bool streaming,
        bool recording,
        bool replay_buffer,
        bool virtual_camera,
        bool capture_risk) noexcept;

    [[nodiscard]] bool open_control_center() noexcept;
    [[nodiscard]] bool consume_restart_request() noexcept;

private:
    [[nodiscard]] std::wstring find_sibling_path(
        const wchar_t *file_name) const;
    void publish_locked(
        std::uint32_t flags, DWORD hud_process_id) noexcept;

    SRWLOCK lock_ = SRWLOCK_INIT;
    UniqueHandle mapping_;
    UniqueHandle status_changed_event_;
    UniqueHandle restart_event_;
    ControlStatus *status_ = nullptr;
    std::wstring mapping_name_;
    std::wstring status_event_name_;
    std::wstring restart_event_name_;
    std::uint32_t last_flags_ = ControlStatusNone;
    DWORD last_hud_process_id_ = 0U;
    std::uint64_t generation_ = 0U;
};

} // namespace chatview
