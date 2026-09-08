// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/shared-state-reader.hpp"

#include <Windows.h>

namespace chatview {

SharedStateReader::~SharedStateReader()
{
    close();
}

bool SharedStateReader::open(const std::wstring &mapping_name,
                             const std::wstring &event_name,
                             DWORD parent_process_id)
{
    close();

    mapping_.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapping_name.c_str()));
    if (!mapping_) {
        return false;
    }

    shared_state_ = static_cast<SharedState *>(
        MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0U, 0U, sizeof(SharedState)));
    if (shared_state_ == nullptr) {
        close();
        return false;
    }

    state_changed_event_.reset(OpenEventW(SYNCHRONIZE, FALSE, event_name.c_str()));
    if (!state_changed_event_) {
        close();
        return false;
    }

    parent_process_.reset(OpenProcess(SYNCHRONIZE, FALSE, parent_process_id));
    if (!parent_process_) {
        close();
        return false;
    }

    return true;
}

void SharedStateReader::close() noexcept
{
    if (shared_state_ != nullptr) {
        UnmapViewOfFile(shared_state_);
        shared_state_ = nullptr;
    }

    parent_process_.reset();
    state_changed_event_.reset();
    mapping_.reset();
}

bool SharedStateReader::read(SharedSnapshot &snapshot) const noexcept
{
    if (shared_state_ == nullptr || shared_state_->magic != kSharedStateMagic ||
        shared_state_->version != kSharedStateVersion) {
        return false;
    }

    for (unsigned int attempt = 0U; attempt < 32U; ++attempt) {
        const LONG before = InterlockedCompareExchange(&shared_state_->sequence, 0, 0);
        if ((before & 1L) != 0L) {
            SwitchToThread();
            continue;
        }

        MemoryBarrier();
        SharedSnapshot candidate;
        candidate.flags = shared_state_->flags;
        candidate.generation = shared_state_->generation;
        MemoryBarrier();

        const LONG after = InterlockedCompareExchange(&shared_state_->sequence, 0, 0);
        if (before == after && (after & 1L) == 0L) {
            snapshot = candidate;
            return true;
        }
    }

    return false;
}

HANDLE SharedStateReader::state_changed_event() const noexcept
{
    return state_changed_event_.get();
}

HANDLE SharedStateReader::parent_process() const noexcept
{
    return parent_process_.get();
}

} // namespace chatview
