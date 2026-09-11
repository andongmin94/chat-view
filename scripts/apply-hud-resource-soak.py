from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def write_new(path_text: str, content: str) -> None:
    path = Path(path_text)
    if path.exists():
        raise SystemExit(f"{path_text}: file already exists")
    path.write_text(content, encoding="utf-8", newline="\n")


replace_once(
    "src/preflight/hud-self-test.cpp",
    """#include <Windows.h>
#include <dwmapi.h>

#include <algorithm>
""",
    """#include <Windows.h>
#include <dwmapi.h>
#include <psapi.h>

#include <algorithm>
""",
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    """#include <array>
#include <chrono>
#include <cstdint>
""",
    """#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>
""",
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    """constexpr DWORD kJobGracefulDrainTimeoutMs = 2000U;
constexpr DWORD kJobForcedDrainTimeoutMs = 5000U;
""",
    """constexpr DWORD kJobGracefulDrainTimeoutMs = 2000U;
constexpr DWORD kJobForcedDrainTimeoutMs = 5000U;
constexpr wchar_t kSoakCyclesEnvironment[] =
    L\"CHATVIEW_HUD_SOAK_CYCLES\";
constexpr unsigned int kMaximumSoakCycles = 32U;
constexpr DWORD kSoakSettleTimeMs = 500U;
constexpr DWORD kMaximumHudHandleGrowth = 64U;
constexpr SIZE_T kMaximumHudPrivateGrowth =
    static_cast<SIZE_T>(128U) * 1024U * 1024U;
constexpr DWORD kMaximumJobProcessGrowth = 4U;
""",
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    """bool drain_child_job(HANDLE job) noexcept
{
    if (wait_for_job_empty(job, kJobGracefulDrainTimeoutMs)) {
        return true;
    }
    if (!TerminateJobObject(job, 0U)) {
        return false;
    }
    return wait_for_job_empty(job, kJobForcedDrainTimeoutMs);
}

void publish(
""",
    """bool drain_child_job(HANDLE job) noexcept
{
    if (wait_for_job_empty(job, kJobGracefulDrainTimeoutMs)) {
        return true;
    }
    if (!TerminateJobObject(job, 0U)) {
        return false;
    }
    return wait_for_job_empty(job, kJobForcedDrainTimeoutMs);
}

struct HudResourceSample {
    DWORD handle_count = 0U;
    SIZE_T private_bytes = 0U;
    DWORD active_processes = 0U;
};

unsigned int requested_soak_cycles() noexcept
{
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(
        kSoakCyclesEnvironment,
        value,
        static_cast<DWORD>(std::size(value)));
    if (length == 0U) {
        return 0U;
    }
    if (length >= std::size(value)) {
        return std::numeric_limits<unsigned int>::max();
    }

    errno = 0;
    wchar_t *end = nullptr;
    const unsigned long parsed = std::wcstoul(value, &end, 10);
    if (errno == ERANGE || end == value || end == nullptr ||
        *end != L'\\0' || parsed == 0UL ||
        parsed > static_cast<unsigned long>(kMaximumSoakCycles)) {
        return std::numeric_limits<unsigned int>::max();
    }
    return static_cast<unsigned int>(parsed);
}

bool query_hud_resource_sample(
    HANDLE process,
    HANDLE job,
    HudResourceSample &sample) noexcept
{
    DWORD handle_count = 0U;
    if (!GetProcessHandleCount(process, &handle_count)) {
        return false;
    }

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!K32GetProcessMemoryInfo(
            process,
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
            sizeof(memory))) {
        return false;
    }

    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    if (!QueryInformationJobObject(
            job,
            JobObjectBasicAccountingInformation,
            &accounting,
            static_cast<DWORD>(sizeof(accounting)),
            nullptr)) {
        return false;
    }

    sample.handle_count = handle_count;
    sample.private_bytes = memory.PrivateUsage;
    sample.active_processes = accounting.ActiveProcesses;
    return true;
}

void update_minimum_sample(
    HudResourceSample &minimum,
    const HudResourceSample &sample,
    bool initialized) noexcept
{
    if (!initialized) {
        minimum = sample;
        return;
    }
    minimum.handle_count = std::min(
        minimum.handle_count, sample.handle_count);
    minimum.private_bytes = std::min(
        minimum.private_bytes, sample.private_bytes);
    minimum.active_processes = std::min(
        minimum.active_processes, sample.active_processes);
}

void publish(
""",
)

replace_once(
    "src/preflight/hud-self-test.cpp",
    """    const std::filesystem::path placement_file =
        local_app_data / L\"ChatView\" / L\"hud.ini\";
""",
    """    const unsigned int soak_cycles = requested_soak_cycles();
    if (soak_cycles == std::numeric_limits<unsigned int>::max()) {
        return fail(
            L\"CHATVIEW_HUD_SOAK_CYCLES must be an integer from 1 to 32\",
            child_process.get());
    }

    if (soak_cycles != 0U) {
        const UINT config_changed_message = RegisterWindowMessageW(
            chatview::kConfigChangedMessageName);
        if (config_changed_message == 0U) {
            return fail(
                L\"Failed to register the soak configuration message\",
                child_process.get());
        }

        HudResourceSample minimum{};
        HudResourceSample latest{};
        bool minimum_initialized = false;

        for (unsigned int cycle = 0U; cycle < soak_cycles; ++cycle) {
            if (!PostMessageW(
                    window, config_changed_message, 0U, 0L)) {
                return fail(
                    L\"Failed to refresh the chat page during the soak\",
                    child_process.get());
            }
            Sleep(kSoakSettleTimeMs);

            publish(
                mapped_state.get(),
                state_event.get(),
                chatview::SharedStateStreaming |
                    chatview::SharedStateCaptureRisk);
            if (!wait_for_visibility(window, false)) {
                return fail(
                    L\"The soak HUD did not hide for capture risk\",
                    child_process.get());
            }

            publish(
                mapped_state.get(),
                state_event.get(),
                chatview::SharedStateStreaming);
            if (!wait_for_visibility(window, true)) {
                return fail(
                    L\"The soak HUD did not return after capture risk\",
                    child_process.get());
            }

            if ((cycle & 1U) == 0U) {
                if (SendMessageW(
                        window,
                        WM_POWERBROADCAST,
                        PBT_APMSUSPEND,
                        0L) != TRUE ||
                    !wait_for_visibility(window, false)) {
                    return fail(
                        L\"The soak HUD did not hide for suspend\",
                        child_process.get());
                }
                const WPARAM resume_event =
                    cycle + 1U == soak_cycles
                        ? PBT_APMRESUMECRITICAL
                        : PBT_APMRESUMEAUTOMATIC;
                if (SendMessageW(
                        window,
                        WM_POWERBROADCAST,
                        resume_event,
                        0L) != TRUE ||
                    !wait_for_visibility(window, true)) {
                    return fail(
                        L\"The soak HUD did not recover from suspend\",
                        child_process.get());
                }
            } else {
                SendMessageW(
                    window,
                    WM_WTSSESSION_CHANGE,
                    WTS_SESSION_LOCK,
                    0L);
                if (!wait_for_visibility(window, false)) {
                    return fail(
                        L\"The soak HUD did not hide for session lock\",
                        child_process.get());
                }
                SendMessageW(
                    window,
                    WM_WTSSESSION_CHANGE,
                    WTS_SESSION_UNLOCK,
                    0L);
                if (!wait_for_visibility(window, true)) {
                    return fail(
                        L\"The soak HUD did not recover from session lock\",
                        child_process.get());
                }
            }

            Sleep(kSoakSettleTimeMs);
            if (!query_hud_resource_sample(
                    child_process.get(), child_job.get(), latest)) {
                return fail(
                    L\"Failed to sample HUD resources during the soak\",
                    child_process.get());
            }

            update_minimum_sample(
                minimum, latest, minimum_initialized);
            minimum_initialized = true;
            std::wcout
                << L\"HUD soak cycle \" << cycle + 1U << L\"/\"
                << soak_cycles << L\": handles=\"
                << latest.handle_count << L\", private_bytes=\"
                << latest.private_bytes << L\", job_processes=\"
                << latest.active_processes << L'\\n';
        }

        if (latest.handle_count >
            minimum.handle_count + kMaximumHudHandleGrowth) {
            return fail(
                L\"HUD handle usage grew beyond the soak envelope\",
                child_process.get());
        }
        if (latest.private_bytes >
            minimum.private_bytes + kMaximumHudPrivateGrowth) {
            return fail(
                L\"HUD private memory grew beyond the soak envelope\",
                child_process.get());
        }
        if (latest.active_processes >
            minimum.active_processes + kMaximumJobProcessGrowth) {
            return fail(
                L\"HUD child-process count grew beyond the soak envelope\",
                child_process.get());
        }
    }

    const std::filesystem::path placement_file =
        local_app_data / L\"ChatView\" / L\"hud.ini\";
""",
)

write_new(
    "scripts/run-hud-soak.ps1",
    r'''[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$SelfTestPath,

    [Parameter(Mandatory)]
    [string]$HudPath,

    [ValidateRange(1, 32)]
    [int]$Cycles = 8
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

$selfTest = (Resolve-Path -LiteralPath $SelfTestPath).Path
$hud = (Resolve-Path -LiteralPath $HudPath).Path
$baselineHudIds = @(
    Get-Process -Name 'chat-view-hud' -ErrorAction SilentlyContinue |
        ForEach-Object Id
)
$baselineProfiles = @(
    Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'chatview-hud-smoke-*' -ErrorAction SilentlyContinue |
        ForEach-Object FullName
)
$previousCycles = $env:CHATVIEW_HUD_SOAK_CYCLES

try {
    $env:CHATVIEW_HUD_SOAK_CYCLES = [string]$Cycles
    & $selfTest $hud
    if ($LASTEXITCODE -ne 0) {
        throw "HUD resource soak failed with exit code $LASTEXITCODE."
    }
}
finally {
    if ($null -eq $previousCycles) {
        Remove-Item Env:CHATVIEW_HUD_SOAK_CYCLES -ErrorAction SilentlyContinue
    }
    else {
        $env:CHATVIEW_HUD_SOAK_CYCLES = $previousCycles
    }
}

$newHudProcesses = @(
    Get-Process -Name 'chat-view-hud' -ErrorAction SilentlyContinue |
        Where-Object { $_.Id -notin $baselineHudIds }
)
if ($newHudProcesses.Count -ne 0) {
    $ids = ($newHudProcesses | ForEach-Object Id) -join ', '
    throw "HUD resource soak left orphan processes: $ids"
}

$newProfiles = @(
    Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'chatview-hud-smoke-*' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notin $baselineProfiles }
)
if ($newProfiles.Count -ne 0) {
    $paths = ($newProfiles | ForEach-Object FullName) -join '; '
    throw "HUD resource soak left profile directories: $paths"
}

$orphanWebViews = @(
    Get-CimInstance Win32_Process -Filter "Name = 'msedgewebview2.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match 'chatview-hud-smoke-' }
)
if ($orphanWebViews.Count -ne 0) {
    $ids = ($orphanWebViews | ForEach-Object ProcessId) -join ', '
    throw "HUD resource soak left WebView2 processes: $ids"
}

Write-Host "HUD resource soak completed: $Cycles cycles, no orphan process or profile."
''',
)

replace_once(
    ".github/workflows/windows-build.yml",
    """          ctest --test-dir build/windows-x64 `
            --build-config RelWithDebInfo `
            --repeat until-fail:5 `
            --output-on-failure

          cmake --install build/windows-x64 `
""",
    """          ctest --test-dir build/windows-x64 `
            --build-config RelWithDebInfo `
            --repeat until-fail:5 `
            --output-on-failure

          & ./scripts/run-hud-soak.ps1 `
            -SelfTestPath build/windows-x64/RelWithDebInfo/chat-view-self-test.exe `
            -HudPath build/windows-x64/rundir/obs-plugins/64bit/chat-view-hud.exe `
            -Cycles 8

          cmake --install build/windows-x64 `
""",
)
