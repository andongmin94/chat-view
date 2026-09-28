// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/shared-state.hpp"
#include "common/win32-handle.hpp"

#include <Windows.h>

#include <string>

namespace chatview {

class SharedStateReader final {
public:
    SharedStateReader() = default;
    ~SharedStateReader();

    SharedStateReader(const SharedStateReader &) = delete;
    SharedStateReader &operator=(const SharedStateReader &) = delete;

    [[nodiscard]] bool open(const std::wstring &mapping_name,
                            const std::wstring &event_name,
                            DWORD parent_process_id);
    void close() noexcept;

    [[nodiscard]] bool read(SharedSnapshot &snapshot) const noexcept;
    [[nodiscard]] ObsOutputObservation read_outputs() const noexcept
    {
        SharedSnapshot snapshot;
        if (!parent_process_ || WaitForSingleObject(parent_process_.get(), 0U) != WAIT_TIMEOUT ||
            !read(snapshot) || has_flag(snapshot, SharedStateShutdown)) return {};
        return read_obs_output(*shared_state_);
    }
    [[nodiscard]] HANDLE state_changed_event() const noexcept;
    [[nodiscard]] HANDLE parent_process() const noexcept;

private:
    UniqueHandle mapping_;
    UniqueHandle state_changed_event_;
    UniqueHandle parent_process_;
    const SharedState *shared_state_ = nullptr;
};

} // namespace chatview
