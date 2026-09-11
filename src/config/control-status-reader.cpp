// SPDX-License-Identifier: GPL-2.0-or-later

#include "config/control-status-reader.hpp"

#include <Windows.h>

namespace chatview {

ControlStatusReader::~ControlStatusReader()
{
    close();
}

bool ControlStatusReader::open(
    const std::wstring &mapping_name,
    const std::wstring &status_event_name,
    const std::wstring &restart_event_name,
    DWORD parent_process_id) noexcept
{
    close();

    if (mapping_name.empty() || status_event_name.empty() ||
        restart_event_name.empty() || parent_process_id == 0U) {
        return false;
    }

    mapping_.reset(OpenFileMappingW(
        FILE_MAP_READ, FALSE, mapping_name.c_str()));
    if (!mapping_) {
        return false;
    }

    status_ = static_cast<ControlStatus *>(MapViewOfFile(
        mapping_.get(), FILE_MAP_READ, 0U, 0U, sizeof(ControlStatus)));
    if (status_ == nullptr) {
        close();
        return false;
    }

    if (status_->magic != kControlStatusMagic ||
        status_->version != kControlStatusVersion) {
        close();
        return false;
    }

    status_changed_event_.reset(OpenEventW(
        SYNCHRONIZE, FALSE, status_event_name.c_str()));
    if (!status_changed_event_) {
        close();
        return false;
    }

    restart_event_.reset(OpenEventW(
        EVENT_MODIFY_STATE, FALSE, restart_event_name.c_str()));
    if (!restart_event_) {
        close();
        return false;
    }

    parent_process_.reset(OpenProcess(
        SYNCHRONIZE, FALSE, parent_process_id));
    if (!parent_process_) {
        close();
        return false;
    }

    return true;
}

void ControlStatusReader::close() noexcept
{
    if (status_ != nullptr) {
        UnmapViewOfFile(status_);
        status_ = nullptr;
    }

    parent_process_.reset();
    restart_event_.reset();
    status_changed_event_.reset();
    mapping_.reset();
}

bool ControlStatusReader::read(
    ControlStatusSnapshot &snapshot) const noexcept
{
    if (status_ == nullptr || status_->magic != kControlStatusMagic ||
        status_->version != kControlStatusVersion) {
        return false;
    }

    for (unsigned int attempt = 0U; attempt < 32U; ++attempt) {
        const LONG before = InterlockedCompareExchange(
            &status_->sequence, 0, 0);
        if ((before & 1L) != 0L) {
            SwitchToThread();
            continue;
        }

        MemoryBarrier();
        ControlStatusSnapshot candidate;
        candidate.flags = status_->flags;
        candidate.hud_process_id = status_->hud_process_id;
        candidate.generation = status_->generation;
        candidate.updated_tick_ms = status_->updated_tick_ms;
        MemoryBarrier();

        const LONG after = InterlockedCompareExchange(
            &status_->sequence, 0, 0);
        if (before == after && (after & 1L) == 0L &&
            is_valid_control_status_snapshot(candidate)) {
            snapshot = candidate;
            return true;
        }
    }

    return false;
}

bool ControlStatusReader::parent_alive() const noexcept
{
    return parent_process_ &&
           WaitForSingleObject(parent_process_.get(), 0U) == WAIT_TIMEOUT;
}

bool ControlStatusReader::request_restart() const noexcept
{
    return parent_alive() && restart_event_ &&
           SetEvent(restart_event_.get()) != FALSE;
}

HANDLE ControlStatusReader::status_changed_event() const noexcept
{
    return status_changed_event_.get();
}

} // namespace chatview
