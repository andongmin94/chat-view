// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

namespace chatview {

class SharedSrwLockGuard final {
public:
    explicit SharedSrwLockGuard(SRWLOCK &lock) noexcept : lock_(&lock)
    {
        AcquireSRWLockShared(lock_);
    }

    ~SharedSrwLockGuard()
    {
        ReleaseSRWLockShared(lock_);
    }

    SharedSrwLockGuard(const SharedSrwLockGuard &) = delete;
    SharedSrwLockGuard &operator=(const SharedSrwLockGuard &) = delete;

private:
    SRWLOCK *lock_ = nullptr;
};

class ExclusiveSrwLockGuard final {
public:
    explicit ExclusiveSrwLockGuard(SRWLOCK &lock) noexcept : lock_(&lock)
    {
        AcquireSRWLockExclusive(lock_);
    }

    ~ExclusiveSrwLockGuard()
    {
        ReleaseSRWLockExclusive(lock_);
    }

    ExclusiveSrwLockGuard(const ExclusiveSrwLockGuard &) = delete;
    ExclusiveSrwLockGuard &operator=(const ExclusiveSrwLockGuard &) = delete;

private:
    SRWLOCK *lock_ = nullptr;
};

} // namespace chatview
