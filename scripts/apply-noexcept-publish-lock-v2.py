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


def replace_exact(path: str, old: str, new: str, expected: int = 1) -> None:
    content = read(path)
    count = content.count(old)
    if count != expected:
        raise RuntimeError(
            f"Expected {expected} occurrence(s) in {path}, found {count}: {old!r}"
        )
    write(path, content.replace(old, new))


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
if '#include "common/srw-lock.hpp"\n' in header:
    raise RuntimeError("runtime-controller.hpp already includes srw-lock.hpp")
if '#include "common/shared-state.hpp"\n' not in header:
    raise RuntimeError("Could not locate shared-state include in runtime-controller.hpp")
header = header.replace(
    '#include "common/shared-state.hpp"\n',
    '#include "common/shared-state.hpp"\n#include "common/srw-lock.hpp"\n',
    1,
)
if header.count("#include <shared_mutex>\n") != 1:
    raise RuntimeError("Expected one <shared_mutex> include in runtime-controller.hpp")
header = header.replace("#include <shared_mutex>\n", "", 1)
if header.count("std::shared_mutex state_publish_mutex_;") != 1:
    raise RuntimeError("Expected one state_publish_mutex_ member")
header = header.replace(
    "std::shared_mutex state_publish_mutex_;",
    "SrwLock state_publish_lock_;",
    1,
)
write(header_path, header)

source_path = "src/plugin/runtime-controller.cpp"
source = read(source_path)
source = source.replace("#include <shared_mutex>\n", "")
shared_pattern = "std::shared_lock publish_lock(state_publish_mutex_);"
exclusive_pattern = "std::unique_lock publish_lock(state_publish_mutex_);"
if source.count(shared_pattern) != 2:
    raise RuntimeError(
        f"Expected two shared publish locks, found {source.count(shared_pattern)}"
    )
if source.count(exclusive_pattern) != 3:
    raise RuntimeError(
        f"Expected three exclusive publish locks, found {source.count(exclusive_pattern)}"
    )
source = source.replace(
    shared_pattern,
    "SharedSrwLockGuard publish_lock(state_publish_lock_);",
)
source = source.replace(
    exclusive_pattern,
    "ExclusiveSrwLockGuard publish_lock(state_publish_lock_);",
)
if "state_publish_mutex_" in source:
    raise RuntimeError("A state_publish_mutex_ reference remains in runtime-controller.cpp")
write(source_path, source)

cmake_path = "CMakeLists.txt"
cmake = read(cmake_path)
version_match = re.search(
    r"project\(chat-view-obs VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)",
    cmake,
)
if version_match is None:
    raise RuntimeError("Could not locate the ChatView project version")
if version_match.group(1) != "0.2.7":
    raise RuntimeError(
        f"Expected project version 0.2.7, found {version_match.group(1)}"
    )
cmake = (
    cmake[: version_match.start(1)]
    + "0.2.8"
    + cmake[version_match.end(1) :]
)
write(cmake_path, cmake)

architecture_path = "docs/architecture.md"
architecture = read(architecture_path)
statement = (
    "OBS frontend callbacks only update the desired state and wake the supervisor; "
    "the potentially slow process work remains off the OBS callback thread."
)
addition = (
    statement
    + " The wake-handle lifetime is protected by non-throwing Windows SRW locks, "
      "so lock acquisition cannot raise a C++ exception through an OBS callback."
)
if addition not in architecture:
    if statement not in architecture:
        raise RuntimeError("Could not locate the callback-thread architecture statement")
    architecture = architecture.replace(statement, addition, 1)
    write(architecture_path, architecture)

for obsolete in (
    ".github/workflows/apply-noexcept-publish-lock.yml",
    "scripts/apply-noexcept-publish-lock.py",
    ".github/workflows/apply-noexcept-publish-lock-v2.yml",
    "scripts/apply-noexcept-publish-lock-v2.py",
):
    (ROOT / obsolete).unlink(missing_ok=True)
