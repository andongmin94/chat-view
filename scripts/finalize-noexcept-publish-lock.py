from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8", newline="\n")


SRW_LOCK_HEADER = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

namespace chatview {

class SharedSrwLockGuard;
class ExclusiveSrwLockGuard;

class SrwLock final {
public:
    constexpr SrwLock() noexcept = default;

    SrwLock(const SrwLock &) = delete;
    SrwLock &operator=(const SrwLock &) = delete;
    SrwLock(SrwLock &&) = delete;
    SrwLock &operator=(SrwLock &&) = delete;

private:
    friend class SharedSrwLockGuard;
    friend class ExclusiveSrwLockGuard;

    SRWLOCK lock_ = SRWLOCK_INIT;
};

class SharedSrwLockGuard final {
public:
    explicit SharedSrwLockGuard(SrwLock &lock) noexcept : lock_(lock)
    {
        AcquireSRWLockShared(&lock_.lock_);
    }

    ~SharedSrwLockGuard() noexcept
    {
        ReleaseSRWLockShared(&lock_.lock_);
    }

    SharedSrwLockGuard(const SharedSrwLockGuard &) = delete;
    SharedSrwLockGuard &operator=(const SharedSrwLockGuard &) = delete;
    SharedSrwLockGuard(SharedSrwLockGuard &&) = delete;
    SharedSrwLockGuard &operator=(SharedSrwLockGuard &&) = delete;

private:
    SrwLock &lock_;
};

class ExclusiveSrwLockGuard final {
public:
    explicit ExclusiveSrwLockGuard(SrwLock &lock) noexcept : lock_(lock)
    {
        AcquireSRWLockExclusive(&lock_.lock_);
    }

    ~ExclusiveSrwLockGuard() noexcept
    {
        ReleaseSRWLockExclusive(&lock_.lock_);
    }

    ExclusiveSrwLockGuard(const ExclusiveSrwLockGuard &) = delete;
    ExclusiveSrwLockGuard &operator=(const ExclusiveSrwLockGuard &) = delete;
    ExclusiveSrwLockGuard(ExclusiveSrwLockGuard &&) = delete;
    ExclusiveSrwLockGuard &operator=(ExclusiveSrwLockGuard &&) = delete;

private:
    SrwLock &lock_;
};

} // namespace chatview
'''

write("src/common/srw-lock.hpp", SRW_LOCK_HEADER)

header_path = "src/plugin/runtime-controller.hpp"
header = read(header_path)
if '#include "common/srw-lock.hpp"\n' not in header:
    anchor = '#include "common/shared-state.hpp"\n'
    if anchor not in header:
        raise RuntimeError("Could not locate shared-state include")
    header = header.replace(anchor, anchor + '#include "common/srw-lock.hpp"\n', 1)
header = header.replace("#include <shared_mutex>\n", "")
old_member = "std::shared_mutex state_publish_mutex_;"
new_member = "SrwLock state_publish_lock_;"
if old_member in header:
    header = header.replace(old_member, new_member, 1)
if header.count(new_member) != 1 or old_member in header:
    raise RuntimeError("Publish-lock member did not converge to SrwLock")
write(header_path, header)

source_path = "src/plugin/runtime-controller.cpp"
source = read(source_path).replace("#include <shared_mutex>\n", "")
source = source.replace(
    "std::shared_lock publish_lock(state_publish_mutex_);",
    "SharedSrwLockGuard publish_lock(state_publish_lock_);",
)
source = source.replace(
    "std::unique_lock publish_lock(state_publish_mutex_);",
    "ExclusiveSrwLockGuard publish_lock(state_publish_lock_);",
)
if "state_publish_mutex_" in source:
    raise RuntimeError("A legacy publish mutex reference remains")
if source.count("SharedSrwLockGuard publish_lock(state_publish_lock_);") != 2:
    raise RuntimeError("Expected exactly two shared SRW publish guards")
if source.count("ExclusiveSrwLockGuard publish_lock(state_publish_lock_);") != 3:
    raise RuntimeError("Expected exactly three exclusive SRW publish guards")
write(source_path, source)

cmake_path = "CMakeLists.txt"
cmake = read(cmake_path)
match = re.search(
    r"project\(chat-view-obs VERSION ([0-9]+)\.([0-9]+)\.([0-9]+) LANGUAGES CXX\)",
    cmake,
)
if match is None:
    raise RuntimeError("Could not locate project version")
version = tuple(int(part) for part in match.groups())
if version < (0, 2, 8):
    cmake = cmake[: match.start()] + (
        "project(chat-view-obs VERSION 0.2.8 LANGUAGES CXX)"
    ) + cmake[match.end() :]
write(cmake_path, cmake)

architecture_path = "docs/architecture.md"
architecture = read(architecture_path)
base = (
    "OBS frontend callbacks only update the desired state and wake the supervisor; "
    "the potentially slow process work remains off the OBS callback thread."
)
expanded = (
    base
    + " The wake-handle lifetime is protected by non-throwing Windows SRW locks, "
      "so lock acquisition cannot raise a C++ exception through an OBS callback."
)
if expanded not in architecture:
    if base not in architecture:
        raise RuntimeError("Could not locate callback-thread architecture statement")
    architecture = architecture.replace(base, expanded, 1)
    write(architecture_path, architecture)

for temporary in (
    ".github/workflows/apply-noexcept-publish-lock.yml",
    "scripts/apply-noexcept-publish-lock.py",
    ".github/workflows/apply-noexcept-publish-lock-v2.yml",
    "scripts/apply-noexcept-publish-lock-v2.py",
    ".github/workflows/finalize-noexcept-publish-lock.yml",
    "scripts/finalize-noexcept-publish-lock.py",
):
    (ROOT / temporary).unlink(missing_ok=True)
