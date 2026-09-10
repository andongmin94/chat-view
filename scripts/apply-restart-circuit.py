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
            f"Expected exactly one anchor in {path}, found {count}: {old[:100]!r}"
        )
    write(path, content.replace(old, new, 1))


def write_new(path: str, content: str) -> None:
    target = ROOT / path
    if target.exists():
        raise RuntimeError(f"Refusing to replace existing file: {path}")
    target.parent.mkdir(parents=True, exist_ok=True)
    write(path, content)


restart_policy = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace chatview {

inline constexpr std::uint32_t kMaximumAutomaticRestartFailures = 6U;
inline constexpr std::uint32_t kInitialAutomaticRestartDelayMs = 500U;
inline constexpr std::uint32_t kMaximumAutomaticRestartDelayMs = 30000U;

class RestartPolicy final {
public:
    constexpr void record_failure() noexcept
    {
        if (failure_count_ < kMaximumAutomaticRestartFailures) {
            ++failure_count_;
        }
    }

    constexpr void reset() noexcept
    {
        failure_count_ = 0U;
    }

    [[nodiscard]] constexpr bool automatic_restart_allowed() const noexcept
    {
        return failure_count_ < kMaximumAutomaticRestartFailures;
    }

    [[nodiscard]] constexpr std::uint32_t failure_count() const noexcept
    {
        return failure_count_;
    }

    [[nodiscard]] constexpr std::uint32_t delay_ms() const noexcept
    {
        if (failure_count_ == 0U || !automatic_restart_allowed()) {
            return 0U;
        }

        std::uint32_t delay = kInitialAutomaticRestartDelayMs;
        for (std::uint32_t index = 1U; index < failure_count_; ++index) {
            if (delay >= kMaximumAutomaticRestartDelayMs / 2U) {
                return kMaximumAutomaticRestartDelayMs;
            }
            delay *= 2U;
        }
        return delay > kMaximumAutomaticRestartDelayMs
                   ? kMaximumAutomaticRestartDelayMs
                   : delay;
    }

private:
    std::uint32_t failure_count_ = 0U;
};

static_assert(RestartPolicy{}.automatic_restart_allowed());
static_assert(RestartPolicy{}.failure_count() == 0U);
static_assert(RestartPolicy{}.delay_ms() == 0U);

} // namespace chatview
'''

restart_policy_test = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#include "plugin/restart-policy.hpp"

#include <array>
#include <iostream>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    chatview::RestartPolicy policy;
    if (!policy.automatic_restart_allowed() ||
        policy.failure_count() != 0U ||
        policy.delay_ms() != 0U) {
        return fail("A new restart policy was not immediately usable");
    }

    constexpr std::array<std::uint32_t, 5U> expected_delays{
        500U,
        1000U,
        2000U,
        4000U,
        8000U,
    };

    for (std::size_t index = 0U; index < expected_delays.size(); ++index) {
        policy.record_failure();
        if (!policy.automatic_restart_allowed() ||
            policy.failure_count() != index + 1U ||
            policy.delay_ms() != expected_delays[index]) {
            return fail("Automatic restart backoff changed unexpectedly");
        }
    }

    policy.record_failure();
    if (policy.automatic_restart_allowed() ||
        policy.failure_count() !=
            chatview::kMaximumAutomaticRestartFailures ||
        policy.delay_ms() != 0U) {
        return fail("The automatic restart circuit did not open at its limit");
    }

    policy.record_failure();
    if (policy.failure_count() !=
        chatview::kMaximumAutomaticRestartFailures) {
        return fail("The restart failure counter did not saturate");
    }

    policy.reset();
    if (!policy.automatic_restart_allowed() ||
        policy.failure_count() != 0U ||
        policy.delay_ms() != 0U) {
        return fail("An explicit reset did not close the restart circuit");
    }

    return 0;
}
'''

write_new("src/plugin/restart-policy.hpp", restart_policy)
write_new("tests/restart-policy-test.cpp", restart_policy_test)

replace_once(
    "src/plugin/runtime-controller.hpp",
    "    void update(bool streaming, bool recording) noexcept;\n"
    "    [[nodiscard]] bool open_settings() const noexcept;\n",
    "    void update(bool streaming, bool recording) noexcept;\n"
    "    [[nodiscard]] bool restart_hud() noexcept;\n"
    "    [[nodiscard]] bool open_settings() const noexcept;\n",
)
replace_once(
    "src/plugin/runtime-controller.hpp",
    "    std::atomic_bool stopping_{true};\n"
    "    std::atomic<std::uint32_t> current_flags_{SharedStateNone};\n",
    "    std::atomic_bool stopping_{true};\n"
    "    std::atomic_bool restart_requested_{false};\n"
    "    std::atomic<std::uint32_t> current_flags_{SharedStateNone};\n",
)

replace_once(
    "src/plugin/runtime-controller.cpp",
    '#include "common/window-messages.hpp"\n'
    '#include "plugin/transport-token.hpp"\n',
    '#include "common/window-messages.hpp"\n'
    '#include "plugin/restart-policy.hpp"\n'
    '#include "plugin/transport-token.hpp"\n',
)
replace_once(
    "src/plugin/runtime-controller.cpp",
    "constexpr DWORD kRuntimeStablePeriodMs = 30000U;\n"
    "constexpr DWORD kRestartInitialDelayMs = 500U;\n"
    "constexpr DWORD kRestartMaximumDelayMs = 30000U;\n",
    "constexpr DWORD kRuntimeStablePeriodMs = 30000U;\n",
)
replace_once(
    "src/plugin/runtime-controller.cpp",
    "DWORD next_restart_delay(DWORD current) noexcept\n"
    "{\n"
    "    if (current == 0U) {\n"
    "        return kRestartInitialDelayMs;\n"
    "    }\n"
    "    if (current >= kRestartMaximumDelayMs / 2U) {\n"
    "        return kRestartMaximumDelayMs;\n"
    "    }\n"
    "    return std::min(kRestartMaximumDelayMs, current * 2U);\n"
    "}\n\n",
    "",
)
replace_once(
    "src/plugin/runtime-controller.cpp",
    "            stopping_.store(false, std::memory_order_release);\n\n"
    "            if (!create_transport_locked()) {\n",
    "            stopping_.store(false, std::memory_order_release);\n"
    "            restart_requested_.store(false, std::memory_order_release);\n\n"
    "            if (!create_transport_locked()) {\n",
)

runtime_path = "src/plugin/runtime-controller.cpp"
runtime = read(runtime_path)
open_settings_marker = "\nbool RuntimeController::open_settings() const noexcept\n"
open_settings_index = runtime.find(open_settings_marker)
if open_settings_index < 0:
    raise RuntimeError("Could not locate RuntimeController::open_settings")
if "bool RuntimeController::restart_hud() noexcept" in runtime:
    raise RuntimeError("RuntimeController::restart_hud already exists")
restart_method = r'''

bool RuntimeController::restart_hud() noexcept
{
    if (stopping_.load(std::memory_order_acquire)) {
        return false;
    }

    restart_requested_.store(true, std::memory_order_release);
    std::shared_lock publish_lock(state_publish_mutex_);
    if (state_publish_handle_ == nullptr) {
        restart_requested_.store(false, std::memory_order_release);
        return false;
    }
    if (!SetEvent(state_publish_handle_)) {
        log_windows_error("SetEvent(HUD restart)", GetLastError());
        restart_requested_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}
'''
runtime = runtime[:open_settings_index] + restart_method + runtime[open_settings_index:]

supervisor_start = runtime.find("void RuntimeController::supervisor_loop() noexcept\n{")
supervisor_end = runtime.find(
    "\nvoid RuntimeController::publish_locked", supervisor_start
)
if supervisor_start < 0 or supervisor_end < 0:
    raise RuntimeError("Could not locate the supervisor loop")

supervisor = r'''void RuntimeController::supervisor_loop() noexcept
{
    RestartPolicy restart_policy;
    ULONGLONG retry_deadline = 0U;

    const auto record_failure = [&restart_policy, &retry_deadline]() noexcept {
        restart_policy.record_failure();
        if (!restart_policy.automatic_restart_allowed()) {
            retry_deadline = 0U;
            blog(
                LOG_ERROR,
                "[ChatView OBS] HUD automatic restart disabled after %u "
                "consecutive failures; use Tools > Restart ChatView HUD",
                static_cast<unsigned int>(restart_policy.failure_count()));
            return;
        }

        const DWORD delay =
            static_cast<DWORD>(restart_policy.delay_ms());
        retry_deadline = GetTickCount64() + delay;
        blog(
            LOG_WARNING,
            "[ChatView OBS] HUD restart scheduled in %lu ms after failure "
            "%u/%u",
            static_cast<unsigned long>(delay),
            static_cast<unsigned int>(restart_policy.failure_count()),
            static_cast<unsigned int>(
                kMaximumAutomaticRestartFailures));
    };

    try {
        while (!stopping_.load(std::memory_order_acquire)) {
            HANDLE stop_event = nullptr;
            HANDLE publish_event = nullptr;
            HANDLE process = nullptr;
            DWORD process_id = 0U;
            {
                std::scoped_lock lock(mutex_);
                stop_event = supervisor_stop_event_.get();
                publish_event = state_publish_event_.get();
                process = runtime_process_.get();
                process_id = runtime_process_id_;
            }

            if (stop_event == nullptr || publish_event == nullptr) {
                return;
            }

            if (process == nullptr) {
                DWORD wait_timeout = INFINITE;
                if (restart_policy.automatic_restart_allowed()) {
                    const ULONGLONG now = GetTickCount64();
                    const ULONGLONG remaining =
                        retry_deadline > now ? retry_deadline - now : 0U;
                    wait_timeout =
                        remaining >= static_cast<ULONGLONG>(INFINITE)
                            ? INFINITE - 1U
                            : static_cast<DWORD>(remaining);
                }

                HANDLE wait_handles[2] = {stop_event, publish_event};
                const DWORD wait_result = WaitForMultipleObjects(
                    2U, wait_handles, FALSE, wait_timeout);
                if (wait_result == WAIT_OBJECT_0) {
                    return;
                }
                if (wait_result == WAIT_OBJECT_0 + 1U) {
                    const bool explicit_restart =
                        restart_requested_.exchange(
                            false, std::memory_order_acq_rel);
                    if (!explicit_restart) {
                        continue;
                    }

                    restart_policy.reset();
                    retry_deadline = 0U;
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] Manual HUD restart requested");
                } else if (wait_result == WAIT_FAILED) {
                    log_windows_error(
                        "WaitForMultipleObjects(HUD restart backoff)",
                        GetLastError());
                    return;
                } else if (wait_result != WAIT_TIMEOUT) {
                    blog(
                        LOG_ERROR,
                        "[ChatView OBS] HUD restart wait returned unexpected "
                        "result %lu",
                        static_cast<unsigned long>(wait_result));
                    return;
                }

                if (!restart_policy.automatic_restart_allowed()) {
                    continue;
                }

                bool launched = false;
                {
                    std::scoped_lock lock(mutex_);
                    if (stopping_.load(std::memory_order_acquire)) {
                        return;
                    }
                    launched = launch_runtime_locked();
                    if (launched) {
                        publish_locked(
                            current_flags_.load(
                                std::memory_order_acquire));
                    }
                }

                if (stopping_.load(std::memory_order_acquire)) {
                    return;
                }
                if (launched) {
                    retry_deadline = 0U;
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] HUD runtime started and reported "
                        "ready");
                } else {
                    record_failure();
                }
                continue;
            }

            HANDLE wait_handles[3] = {
                stop_event,
                process,
                publish_event,
            };
            const DWORD wait_result = WaitForMultipleObjects(
                3U,
                wait_handles,
                FALSE,
                kRuntimeStablePeriodMs);
            if (wait_result == WAIT_OBJECT_0) {
                return;
            }
            if (wait_result == WAIT_OBJECT_0 + 2U) {
                const bool explicit_restart =
                    restart_requested_.exchange(
                        false, std::memory_order_acq_rel);

                std::scoped_lock lock(mutex_);
                if (runtime_process_id_ != process_id ||
                    runtime_process_.get() != process) {
                    continue;
                }

                if (explicit_restart) {
                    if (WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
                        terminate_runtime_locked(
                            "was restarted from the OBS Tools menu");
                    } else {
                        runtime_process_.reset();
                        runtime_process_id_ = 0U;
                        runtime_job_.reset();
                    }
                    restart_policy.reset();
                    retry_deadline = 0U;
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] Manual HUD restart accepted");
                } else if (
                    WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
                    publish_locked(
                        current_flags_.load(
                            std::memory_order_acquire));
                }
                continue;
            }
            if (wait_result == WAIT_TIMEOUT) {
                if (restart_policy.failure_count() != 0U) {
                    blog(
                        LOG_INFO,
                        "[ChatView OBS] HUD remained stable; restart "
                        "failure counter reset");
                }
                restart_policy.reset();
                retry_deadline = 0U;
                continue;
            }
            if (wait_result == WAIT_FAILED) {
                log_windows_error(
                    "WaitForMultipleObjects(HUD supervisor)",
                    GetLastError());
                return;
            }
            if (wait_result != WAIT_OBJECT_0 + 1U) {
                blog(
                    LOG_ERROR,
                    "[ChatView OBS] HUD supervisor received unexpected "
                    "wait result %lu",
                    static_cast<unsigned long>(wait_result));
                return;
            }

            bool exited = false;
            {
                std::scoped_lock lock(mutex_);
                if (runtime_process_id_ == process_id &&
                    runtime_process_.get() == process) {
                    log_process_exit(
                        runtime_process_.get(), "exited unexpectedly");
                    runtime_process_.reset();
                    runtime_process_id_ = 0U;
                    runtime_job_.reset();
                    exited = true;
                }
            }
            if (exited) {
                record_failure();
            }
        }
    } catch (const std::exception &error) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD supervisor failed: %s",
            error.what());
    } catch (...) {
        blog(
            LOG_ERROR,
            "[ChatView OBS] HUD supervisor failed with an unknown "
            "exception");
    }

    stopping_.store(true, std::memory_order_release);
    try {
        std::scoped_lock lock(mutex_);
        terminate_runtime_locked("lost its supervisor");
    } catch (...) {
        if (runtime_job_) {
            TerminateJobObject(runtime_job_.get(), 1U);
        }
    }
}
'''
runtime = runtime[:supervisor_start] + supervisor + runtime[supervisor_end:]

cleanup_anchor = (
    "    generation_ = 0U;\n"
    "    current_flags_.store(SharedStateNone, std::memory_order_release);\n"
)
if runtime.count(cleanup_anchor) != 1:
    raise RuntimeError("Could not locate cleanup atomic reset anchor")
runtime = runtime.replace(
    cleanup_anchor,
    "    generation_ = 0U;\n"
    "    restart_requested_.store(false, std::memory_order_release);\n"
    "    current_flags_.store(SharedStateNone, std::memory_order_release);\n",
    1,
)
write(runtime_path, runtime)

replace_once(
    "src/plugin/plugin-main.cpp",
    "void toggle_edit_mode(void *)\n"
    "{\n"
    "    if (runtime_controller && !runtime_controller->toggle_edit_mode()) {\n",
    "void restart_hud(void *)\n"
    "{\n"
    "    if (runtime_controller && !runtime_controller->restart_hud()) {\n"
    "        blog(LOG_ERROR, \"[ChatView OBS] HUD restart could not be requested\");\n"
    "    }\n"
    "}\n\n"
    "void toggle_edit_mode(void *)\n"
    "{\n"
    "    if (runtime_controller && !runtime_controller->toggle_edit_mode()) {\n",
)
replace_once(
    "src/plugin/plugin-main.cpp",
    "        obs_frontend_add_tools_menu_item(\n"
    "            obs_module_text(\"ChatView.EditOverlay\"), toggle_edit_mode, nullptr);\n",
    "        obs_frontend_add_tools_menu_item(\n"
    "            obs_module_text(\"ChatView.RestartHud\"), restart_hud, nullptr);\n"
    "        obs_frontend_add_tools_menu_item(\n"
    "            obs_module_text(\"ChatView.EditOverlay\"), toggle_edit_mode, nullptr);\n",
)

replace_once(
    "data/locale/en-US.ini",
    'ChatView.EditOverlay="Move / Resize ChatView..."\n',
    'ChatView.RestartHud="Restart ChatView HUD"\n'
    'ChatView.EditOverlay="Move / Resize ChatView..."\n',
)
replace_once(
    "data/locale/ko-KR.ini",
    'ChatView.EditOverlay="챗뷰 이동 / 크기 조정..."\n',
    'ChatView.RestartHud="챗뷰 HUD 다시 시작"\n'
    'ChatView.EditOverlay="챗뷰 이동 / 크기 조정..."\n',
)

replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.2.4 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.2.5 LANGUAGES CXX)",
)
replace_once(
    "CMakeLists.txt",
    "    src/plugin/runtime-controller.hpp\n"
    "    src/plugin/transport-token.cpp\n",
    "    src/plugin/runtime-controller.hpp\n"
    "    src/plugin/restart-policy.hpp\n"
    "    src/plugin/transport-token.cpp\n",
)
replace_once(
    "CMakeLists.txt",
    "    add_test(\n"
    "        NAME chat-view-transport-token\n"
    "        COMMAND chat-view-transport-token-test\n"
    "    )\n\n"
    "    add_test(\n"
    "        NAME chat-view-hud-smoke\n",
    "    add_test(\n"
    "        NAME chat-view-transport-token\n"
    "        COMMAND chat-view-transport-token-test\n"
    "    )\n\n"
    "    add_executable(chat-view-restart-policy-test\n"
    "        tests/restart-policy-test.cpp\n"
    "        src/plugin/restart-policy.hpp\n"
    "    )\n"
    "    target_include_directories(\n"
    "        chat-view-restart-policy-test PRIVATE \"${CHATVIEW_SOURCE_DIR}\")\n"
    "    chatview_enable_warnings(chat-view-restart-policy-test)\n"
    "    add_test(\n"
    "        NAME chat-view-restart-policy\n"
    "        COMMAND chat-view-restart-policy-test\n"
    "    )\n\n"
    "    add_test(\n"
    "        NAME chat-view-hud-smoke\n",
)

replace_once(
    "README.md",
    "- bounded shutdown and automatic HUD restart on a later OBS state update;\n",
    "- bounded shutdown and an automatic restart circuit that stops crash loops;\n"
    "- explicit **Tools → Restart ChatView HUD** recovery without restarting OBS;\n",
)
replace_once(
    "README.md",
    "The installer verifies the Microsoft Edge WebView2 Runtime and installs it when missing. It then copies the plugin, HUD, settings application, and locale files into the default OBS directory:\n",
    "The installer verifies the package manifest and the Microsoft Edge WebView2 Runtime, then runs a calibrated local pixel probe before copying any files. The probe must observe an ordinary foreground window and then observe the measured background after Windows capture exclusion is enabled. It then copies the plugin, HUD, settings application, and locale files into the default OBS directory:\n",
)

replace_once(
    "docs/architecture.md",
    "Names are scoped to the current Windows session and the OBS process ID.\n",
    "Names are scoped to the current Windows session, the OBS process ID, and a per-launch CNG random token.\n",
)
replace_once(
    "docs/architecture.md",
    "- The plugin can restart an unexpectedly exited HUD on a later frontend-state update.\n",
    "- Unexpected HUD exits use bounded exponential backoff and open an automatic-restart circuit after six consecutive failures.\n"
    "- **Tools → Restart ChatView HUD** explicitly resets the circuit and replaces a running HUD without restarting OBS.\n",
)
replace_once(
    "docs/architecture.md",
    "- a real WebView2 HUD process smoke test covering initialization readiness, locked/edit modes, native resize, persistence, capture-exclusion request, shared-state shutdown, and delayed WebView profile cleanup;\n",
    "- a calibrated pixel-level Windows capture-exclusion capability probe followed by a real WebView2 HUD process smoke test covering initialization readiness, locked/edit modes, native resize, persistence, capture-exclusion request, shared-state shutdown, and delayed WebView profile cleanup;\n"
    "- deterministic restart-policy tests covering backoff, circuit opening, saturation, and explicit reset;\n",
)

print("Restart circuit hardening applied successfully.")
