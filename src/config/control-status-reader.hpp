// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/control-status.hpp"
#include "common/win32-handle.hpp"

#include <Windows.h>

#include <string>

namespace chatview {

class ControlStatusReader final {
public:
    ControlStatusReader() = default;
    ~ControlStatusReader();

    ControlStatusReader(const ControlStatusReader &) = delete;
    ControlStatusReader &operator=(const ControlStatusReader &) = delete;

    [[nodiscard]] bool open(
        const std::wstring &mapping_name,
        const std::wstring &status_event_name,
        const std::wstring &restart_event_name,
        DWORD parent_process_id) noexcept;
    void close() noexcept;

    [[nodiscard]] bool read(ControlStatusSnapshot &snapshot) const noexcept;
    [[nodiscard]] bool parent_alive() const noexcept;
    [[nodiscard]] bool request_restart() const noexcept;
    [[nodiscard]] HANDLE status_changed_event() const noexcept;

private:
    UniqueHandle mapping_;
    UniqueHandle status_changed_event_;
    UniqueHandle restart_event_;
    UniqueHandle parent_process_;
    const ControlStatus *status_ = nullptr;
};

} // namespace chatview
