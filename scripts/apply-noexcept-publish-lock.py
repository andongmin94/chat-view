from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise RuntimeError(
            f"Expected exactly one anchor in {path}, found {count}: {old[:120]!r}"
        )
    write(path, content.replace(old, new, 1))


def replace_count(path: str, old: str, new: str, expected: int) -> None:
    content = read(path)
    count = content.count(old)
    if count != expected:
        raise RuntimeError(
            f"Expected {expected} anchors in {path}, found {count}: {old!r}"
        )
    write(path, content.replace(old, new))


def write_new(path: str, content: str) -> None:
    target = ROOT / path
    if target.exists():
        raise RuntimeError(f"Refusing to replace existing file: {path}")
    target.parent.mkdir(parents=True, exist_ok=True)
    write(path, content)


srw_lock = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

#include <type_traits>

namespace chatview {

class SharedSrwLockGuard final {
public:
    explicit SharedSrwLockGuard(SRWLOCK &lock) noexcept : lock_(&lock)
    {
        AcquireSRWLockShared(lock_);
    }

    ~SharedSrwLockGuard() noexcept
    {
        ReleaseSRWLockShared(lock_);
    }

    SharedSrwLockGuard(const SharedSrwLockGuard &) = delete;
    SharedSrwLockGuard &operator=(const SharedSrwLockGuard &) = delete;
    SharedSrwLockGuard(SharedSrwLockGuard &&) = delete;
    SharedSrwLockGuard &operator=(SharedSrwLockGuard &&) = delete;

private:
    SRWLOCK *lock_;
};

class ExclusiveSrwLockGuard final {
public:
    explicit ExclusiveSrwLockGuard(SRWLOCK &lock) noexcept : lock_(&lock)
    {
        AcquireSRWLockExclusive(lock_);
    }

    ~ExclusiveSrwLockGuard() noexcept
    {
        ReleaseSRWLockExclusive(lock_);
    }

    ExclusiveSrwLockGuard(const ExclusiveSrwLockGuard &) = delete;
    ExclusiveSrwLockGuard &operator=(const ExclusiveSrwLockGuard &) = delete;
    ExclusiveSrwLockGuard(ExclusiveSrwLockGuard &&) = delete;
    ExclusiveSrwLockGuard &operator=(ExclusiveSrwLockGuard &&) = delete;

private:
    SRWLOCK *lock_;
};

static_assert(
    std::is_nothrow_constructible_v<SharedSrwLockGuard, SRWLOCK &>);
static_assert(std::is_nothrow_destructible_v<SharedSrwLockGuard>);
static_assert(
    std::is_nothrow_constructible_v<ExclusiveSrwLockGuard, SRWLOCK &>);
static_assert(std::is_nothrow_destructible_v<ExclusiveSrwLockGuard>);

} // namespace chatview
'''
write_new("src/common/srw-lock.hpp", srw_lock)

replace_once(
    "src/plugin/runtime-controller.hpp",
    '#include "common/shared-state.hpp"\n#include "common/win32-handle.hpp"\n',
    '#include "common/shared-state.hpp"\n#include "common/srw-lock.hpp"\n#include "common/win32-handle.hpp"\n',
)
replace_once(
    "src/plugin/runtime-controller.hpp",
    "#include <mutex>\n#include <shared_mutex>\n#include <string>\n",
    "#include <mutex>\n#include <string>\n",
)
replace_once(
    "src/plugin/runtime-controller.hpp",
    "    std::shared_mutex state_publish_mutex_;\n",
    "    SRWLOCK state_publish_lock_ = SRWLOCK_INIT;\n",
)

replace_once(
    "src/plugin/runtime-controller.cpp",
    "#include <filesystem>\n#include <shared_mutex>\n#include <string>\n",
    "#include <filesystem>\n#include <string>\n",
)
replace_count(
    "src/plugin/runtime-controller.cpp",
    "std::shared_lock publish_lock(state_publish_mutex_);",
    "SharedSrwLockGuard publish_lock(state_publish_lock_);",
    2,
)
replace_count(
    "src/plugin/runtime-controller.cpp",
    "std::unique_lock publish_lock(state_publish_mutex_);",
    "ExclusiveSrwLockGuard publish_lock(state_publish_lock_);",
    3,
)

replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.2.7 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.2.8 LANGUAGES CXX)",
)
replace_once(
    "CMakeLists.txt",
    "    src/common/shared-state.hpp\n"
    "    src/common/win32-handle.hpp\n",
    "    src/common/shared-state.hpp\n"
    "    src/common/srw-lock.hpp\n"
    "    src/common/win32-handle.hpp\n",
)

architecture_path = "docs/architecture.md"
architecture = read(architecture_path)
anchor = (
    "OBS frontend callbacks only update the desired state and wake the supervisor; "
    "the potentially slow process work remains off the OBS callback thread."
)
if anchor not in architecture:
    raise RuntimeError("Could not locate callback-thread architecture statement")
architecture = architecture.replace(
    anchor,
    anchor
    + " The wake-handle lifetime is guarded with non-throwing Win32 SRW locks, so a C++ lock exception cannot escape a `noexcept` OBS callback.",
    1,
)
write(architecture_path, architecture)

print("No-throw publish-handle locking applied successfully.")
