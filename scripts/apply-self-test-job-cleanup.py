from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


def main() -> None:
    self_test = ROOT / "src/preflight/hud-self-test.cpp"

    replace_once(
        self_test,
        "constexpr auto kProfileCleanupTimeout = std::chrono::seconds(10);\n",
        "constexpr auto kProfileCleanupTimeout = std::chrono::seconds(10);\n"
        "constexpr DWORD kJobGracefulDrainTimeoutMs = 2000U;\n"
        "constexpr DWORD kJobForcedDrainTimeoutMs = 5000U;\n",
    )

    replace_once(
        self_test,
        "void publish(\n"
        "    chatview::SharedState *state,\n"
        "    HANDLE event,\n"
        "    std::uint32_t flags) noexcept\n"
        "{\n",
        "bool configure_child_job(HANDLE job) noexcept\n"
        "{\n"
        "    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};\n"
        "    limits.BasicLimitInformation.LimitFlags =\n"
        "        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;\n"
        "    return SetInformationJobObject(\n"
        "               job,\n"
        "               JobObjectExtendedLimitInformation,\n"
        "               &limits,\n"
        "               static_cast<DWORD>(sizeof(limits))) != FALSE;\n"
        "}\n\n"
        "bool wait_for_job_empty(HANDLE job, DWORD timeout_ms) noexcept\n"
        "{\n"
        "    const ULONGLONG deadline = GetTickCount64() + timeout_ms;\n"
        "    do {\n"
        "        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};\n"
        "        if (!QueryInformationJobObject(\n"
        "                job,\n"
        "                JobObjectBasicAccountingInformation,\n"
        "                &accounting,\n"
        "                static_cast<DWORD>(sizeof(accounting)),\n"
        "                nullptr)) {\n"
        "            return false;\n"
        "        }\n"
        "        if (accounting.ActiveProcesses == 0U) {\n"
        "            return true;\n"
        "        }\n"
        "        Sleep(50U);\n"
        "    } while (GetTickCount64() < deadline);\n"
        "    return false;\n"
        "}\n\n"
        "bool drain_child_job(HANDLE job) noexcept\n"
        "{\n"
        "    if (wait_for_job_empty(job, kJobGracefulDrainTimeoutMs)) {\n"
        "        return true;\n"
        "    }\n"
        "    if (!TerminateJobObject(job, 0U)) {\n"
        "        return false;\n"
        "    }\n"
        "    return wait_for_job_empty(job, kJobForcedDrainTimeoutMs);\n"
        "}\n\n"
        "void publish(\n"
        "    chatview::SharedState *state,\n"
        "    HANDLE event,\n"
        "    std::uint32_t flags) noexcept\n"
        "{\n",
    )

    replace_once(
        self_test,
        "    STARTUPINFOW startup_info{};\n"
        "    startup_info.cb = sizeof(startup_info);\n"
        "    PROCESS_INFORMATION child_info{};\n"
        "    if (!CreateProcessW(\n"
        "            hud_path.c_str(),\n"
        "            command_line.data(),\n"
        "            nullptr,\n"
        "            nullptr,\n"
        "            FALSE,\n"
        "            CREATE_UNICODE_ENVIRONMENT,\n"
        "            nullptr,\n"
        "            nullptr,\n"
        "            &startup_info,\n"
        "            &child_info)) {\n"
        "        return fail(L\"Failed to start the HUD executable\");\n"
        "    }\n\n"
        "    chatview::UniqueHandle child_thread(child_info.hThread);\n"
        "    chatview::UniqueHandle child_process(child_info.hProcess);\n"
        "    child_thread.reset();\n",
        "    chatview::UniqueHandle child_job(\n"
        "        CreateJobObjectW(nullptr, nullptr));\n"
        "    if (!child_job || !configure_child_job(child_job.get())) {\n"
        "        return fail(L\"Failed to create the HUD process job\");\n"
        "    }\n\n"
        "    STARTUPINFOW startup_info{};\n"
        "    startup_info.cb = sizeof(startup_info);\n"
        "    PROCESS_INFORMATION child_info{};\n"
        "    if (!CreateProcessW(\n"
        "            hud_path.c_str(),\n"
        "            command_line.data(),\n"
        "            nullptr,\n"
        "            nullptr,\n"
        "            FALSE,\n"
        "            CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,\n"
        "            nullptr,\n"
        "            nullptr,\n"
        "            &startup_info,\n"
        "            &child_info)) {\n"
        "        return fail(L\"Failed to start the HUD executable\");\n"
        "    }\n\n"
        "    chatview::UniqueHandle child_thread(child_info.hThread);\n"
        "    chatview::UniqueHandle child_process(child_info.hProcess);\n"
        "    if (!AssignProcessToJobObject(\n"
        "            child_job.get(), child_process.get())) {\n"
        "        TerminateProcess(child_process.get(), 1U);\n"
        "        WaitForSingleObject(child_process.get(), 2000U);\n"
        "        return fail(L\"Failed to assign the HUD to its process job\");\n"
        "    }\n"
        "    if (ResumeThread(child_thread.get()) == static_cast<DWORD>(-1)) {\n"
        "        TerminateJobObject(child_job.get(), 1U);\n"
        "        WaitForSingleObject(child_process.get(), 2000U);\n"
        "        return fail(L\"Failed to resume the HUD process\");\n"
        "    }\n"
        "    child_thread.reset();\n",
    )

    replace_once(
        self_test,
        "    if (!remove_tree_with_retry(local_app_data)) {\n"
        "        return fail(L\"Failed to remove the smoke-test profile directory\");\n"
        "    }\n"
        "    return 0;\n",
        "    if (!drain_child_job(child_job.get())) {\n"
        "        return fail(L\"Failed to drain the HUD process tree\");\n"
        "    }\n"
        "    child_process.reset();\n"
        "    child_job.reset();\n\n"
        "    if (!remove_tree_with_retry(local_app_data)) {\n"
        "        return fail(L\"Failed to remove the smoke-test profile directory\");\n"
        "    }\n"
        "    return 0;\n",
    )

    cmake = ROOT / "CMakeLists.txt"
    replace_once(
        cmake,
        "project(chat-view-obs VERSION 0.2.11 LANGUAGES CXX)",
        "project(chat-view-obs VERSION 0.2.12 LANGUAGES CXX)",
    )


if __name__ == "__main__":
    main()
