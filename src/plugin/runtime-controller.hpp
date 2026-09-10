// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/shared-state.hpp"
#include "common/srw-lock.hpp"
#include "common/win32-handle.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

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
    [[nodiscard]] bool restart_hud() noexcept;
    [[nodiscard]] bool open_settings() const noexcept;
    [[nodiscard]] bool toggle_edit_mode() noexcept;

private:
    [[nodiscard]] bool create_transport_locked();
    [[nodiscard]] bool create_runtime_job_locked();
    [[nodiscard]] bool launch_runtime_locked();
    [[nodiscard]] HWND find_runtime_window_locked() const noexcept;
    [[nodiscard]] std::wstring find_sibling_path(const wchar_t *file_name) const;

    void supervisor_loop() noexcept;
    void publish_locked(std::uint32_t flags) noexcept;
    void terminate_runtime_locked(const char *reason) noexcept;
    void cleanup_locked() noexcept;

    std::mutex mutex_;
    SRWLOCK state_publish_lock_ = SRWLOCK_INIT;
    std::thread supervisor_thread_;
    std::atomic_bool stopping_{true};
    std::atomic_bool restart_requested_{false};
    std::atomic<std::uint32_t> current_flags_{SharedStateNone};
    HANDLE state_publish_handle_ = nullptr;
    UniqueHandle supervisor_stop_event_;
    UniqueHandle state_publish_event_;
    UniqueHandle runtime_job_;
    UniqueHandle mapping_;
    UniqueHandle state_changed_event_;
    UniqueHandle runtime_ready_event_;
    UniqueHandle runtime_process_;
    SharedState *shared_state_ = nullptr;
    std::wstring mapping_name_;
    std::wstring event_name_;
    std::wstring ready_event_name_;
    DWORD runtime_process_id_ = 0U;
    std::uint64_t generation_ = 0U;
};

} // namespace chatview
