from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def replace_between(path_text: str, start: str, end: str, replacement: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    start_index = text.find(start)
    if start_index < 0:
        raise SystemExit(f"{path_text}: start marker not found: {start}")
    end_index = text.find(end, start_index)
    if end_index < 0:
        raise SystemExit(f"{path_text}: end marker not found: {end}")
    path.write_text(
        text[:start_index] + replacement + text[end_index:],
        encoding="utf-8",
        newline="\n",
    )


replace_once(
    "src/common/runtime-history-store.cpp",
    "    std::uint32_t size = sizeof(RuntimeHistoryRecord);",
    "    std::uint32_t size = 64U;",
)

replace_once(
    "src/config/main.cpp",
    '''constexpr int kWindowWidthDip = 920;
constexpr int kWindowHeightDip = 520;
''',
    '''constexpr int kWindowWidthDip = 960;
constexpr int kWindowHeightDip = 570;
''',
)
replace_once(
    "src/config/main.cpp",
    '''struct HealthPresentation {
    std::wstring text;
    COLORREF color = kColorMuted;
};
''',
    '''struct HealthPresentation {
    std::wstring text;
    COLORREF color = kColorMuted;
};

struct RuntimePresentation {
    std::wstring text;
    COLORREF color = kColorMuted;
};
''',
)
replace_once(
    "src/config/main.cpp",
    '''}

BOOL CALLBACK find_hud_window(HWND window, LPARAM data)
''',
    r'''}

std::wstring runtime_restart_reason_name(
    chatview::RuntimeRestartReason reason)
{
    switch (reason) {
    case chatview::RuntimeRestartReason::LaunchFailure:
        return L"HUD launch failed";
    case chatview::RuntimeRestartReason::StartupTimeout:
        return L"HUD startup timed out";
    case chatview::RuntimeRestartReason::WebViewFailure:
        return L"WebView host failed";
    case chatview::RuntimeRestartReason::NavigationFailure:
        return L"chat navigation failed";
    case chatview::RuntimeRestartReason::CaptureExclusionFailure:
        return L"capture exclusion was lost";
    case chatview::RuntimeRestartReason::ReadySignalFailure:
        return L"HUD readiness signal failed";
    case chatview::RuntimeRestartReason::PlacementFailure:
        return L"HUD placement failed";
    case chatview::RuntimeRestartReason::PageHealthTimeout:
        return L"page-health heartbeat timed out";
    case chatview::RuntimeRestartReason::ConnectionRecovery:
        return L"chat connection recovery failed";
    case chatview::RuntimeRestartReason::SystemLifecycleRecovery:
        return L"Windows resume recovery restarted the HUD";
    case chatview::RuntimeRestartReason::UnexpectedExit:
        return L"HUD exited unexpectedly";
    case chatview::RuntimeRestartReason::ManualRestart:
        return L"manual restart";
    case chatview::RuntimeRestartReason::None:
    default:
        return L"unknown reason";
    }
}

std::wstring runtime_exit_suffix(
    const chatview::RuntimeTelemetrySnapshot &telemetry)
{
    return telemetry.last_exit_code ==
                   chatview::kRuntimeExitCodeUnavailable
               ? std::wstring{}
               : L", exit " +
                     std::to_wstring(telemetry.last_exit_code);
}

RuntimePresentation runtime_presentation(
    const chatview::RuntimeTelemetrySnapshot &telemetry)
{
    if (!chatview::has_runtime_telemetry_flag(
            telemetry,
            chatview::RuntimeTelemetryHistoryValid)) {
        return {L"No restart history", kColorMuted};
    }

    const std::wstring reason =
        runtime_restart_reason_name(telemetry.restart_reason);
    const std::wstring exit = runtime_exit_suffix(telemetry);
    if (chatview::has_runtime_telemetry_flag(
            telemetry,
            chatview::RuntimeTelemetryCircuitOpen)) {
        return {
            L"Automatic restart blocked after " +
                std::to_wstring(telemetry.consecutive_failures) +
                L" failures — " + reason + exit,
            kColorError};
    }

    if (telemetry.consecutive_failures != 0U) {
        return {
            L"Automatic recovery pending — " + reason +
                L" (failure " +
                std::to_wstring(telemetry.consecutive_failures) +
                L"/" +
                std::to_wstring(
                    chatview::kMaximumRuntimeFailureCount) +
                L")" + exit,
            kColorWarning};
    }

    if (telemetry.restart_reason ==
        chatview::RuntimeRestartReason::ManualRestart) {
        return {L"Last restart requested manually", kColorMuted};
    }

    return {
        L"Last automatic recovery: " + reason +
            L" — failure counter cleared" + exit,
        kColorGood};
}

BOOL CALLBACK find_hud_window(HWND window, LPARAM data)
''',
)
replace_once(
    "src/config/main.cpp",
    '''        output_label_ = create_static(L"OBS output");
        output_value_ = create_static(L"Checking...");
        feedback_ = create_static(L"");
''',
    '''        output_label_ = create_static(L"OBS output");
        output_value_ = create_static(L"Checking...");
        recovery_label_ = create_static(L"Restart history");
        recovery_value_ = create_static(L"Checking...");
        feedback_ = create_static(L"");
''',
)
replace_once(
    "src/config/main.cpp",
    '''        const std::array<HWND, 19U> required{
''',
    '''        const std::array<HWND, 21U> required{
''',
)
replace_once(
    "src/config/main.cpp",
    '''            output_label_,
            output_value_,
            feedback_,
''',
    '''            output_label_,
            output_value_,
            recovery_label_,
            recovery_value_,
            feedback_,
''',
)
replace_once(
    "src/config/main.cpp",
    '''            set_colored_text(
                output_value_,
                L"●  Unknown",
                kColorMuted,
                output_color_);
            EnableWindow(edit_button_, FALSE);
''',
    '''            set_colored_text(
                output_value_,
                L"●  Unknown",
                kColorMuted,
                output_color_);
            set_colored_text(
                recovery_value_,
                L"●  Unknown",
                kColorMuted,
                recovery_color_);
            EnableWindow(edit_button_, FALSE);
''',
)
replace_once(
    "src/config/main.cpp",
    '''        const HealthPresentation health_status =
            health_available
                ? health_presentation(health)
                : HealthPresentation{
                      L"Chat page status unavailable",
                      kColorMuted};

        if (hud_running && hud_visible) {
''',
    '''        const HealthPresentation health_status =
            health_available
                ? health_presentation(health)
                : HealthPresentation{
                      L"Chat page status unavailable",
                      kColorMuted};
        const RuntimePresentation runtime_status =
            runtime_presentation(snapshot.runtime_telemetry);

        if (hud_running && hud_visible) {
''',
)
replace_once(
    "src/config/main.cpp",
    '''        } else {
            set_colored_text(
                hud_value_,
                L"●  Starting or recovering",
                kColorWarning,
                hud_color_);
        }
''',
    '''        } else if (chatview::has_runtime_telemetry_flag(
                       snapshot.runtime_telemetry,
                       chatview::RuntimeTelemetryCircuitOpen)) {
            set_colored_text(
                hud_value_,
                L"●  Stopped — automatic restart circuit open",
                kColorError,
                hud_color_);
        } else {
            set_colored_text(
                hud_value_,
                L"●  Starting or recovering",
                kColorWarning,
                hud_color_);
        }
''',
)
replace_once(
    "src/config/main.cpp",
    '''        set_colored_text(
            output_value_,
            L"●  " + output_description(snapshot),
            kColorText,
            output_color_);

        EnableWindow(
''',
    '''        set_colored_text(
            output_value_,
            L"●  " + output_description(snapshot),
            kColorText,
            output_color_);
        set_colored_text(
            recovery_value_,
            L"●  " + runtime_status.text,
            runtime_status.color,
            recovery_color_);

        EnableWindow(
''',
)
replace_once(
    "src/config/main.cpp",
    '''        set_colored_text(
            output_value_,
            L"●  Unknown",
            kColorMuted,
            output_color_);
        EnableWindow(edit_button_, FALSE);
''',
    '''        set_colored_text(
            output_value_,
            L"●  Unknown",
            kColorMuted,
            output_color_);
        set_colored_text(
            recovery_value_,
            L"●  Unavailable",
            kColorMuted,
            recovery_color_);
        EnableWindow(edit_button_, FALSE);
''',
)
replace_once(
    "src/config/main.cpp",
    '''        } else if (control == output_value_) {
            color = output_color_;
        } else if (control == feedback_) {
''',
    '''        } else if (control == output_value_) {
            color = output_color_;
        } else if (control == recovery_value_) {
            color = recovery_color_;
        } else if (control == feedback_) {
''',
)
replace_once(
    "src/config/main.cpp",
    '''        const std::array<HWND, 20U> body_controls{
''',
    '''        const std::array<HWND, 22U> body_controls{
''',
)
replace_once(
    "src/config/main.cpp",
    '''            safety_value_,
            output_value_,
            feedback_,
''',
    '''            safety_value_,
            output_value_,
            recovery_value_,
            feedback_,
''',
)
replace_once(
    "src/config/main.cpp",
    '''            safety_label_,
            output_label_};
''',
    '''            safety_label_,
            output_label_,
            recovery_label_};
''',
)
replace_once(
    "src/config/main.cpp",
    '''        const std::array<HWND, 5U> labels{
''',
    '''        const std::array<HWND, 6U> labels{
''',
)
replace_once(
    "src/config/main.cpp",
    '''            safety_label_,
            output_label_};
        for (HWND label : labels) {
''',
    '''            safety_label_,
            output_label_,
            recovery_label_};
        for (HWND label : labels) {
''',
)
replace_once(
    "src/config/main.cpp",
    '''        move(title_, 32, 24, 856, 38);
        move(subtitle_, 34, 61, 852, 24);
        move(url_label_, 34, 105, 300, 22);
        move(url_edit_, 34, 132, 852, 32);
        move(provider_value_, 36, 170, 848, 24);
        move(status_group_, 28, 207, 864, 190);
        move(obs_label_, 52, 239, 190, 24);
        move(obs_value_, 248, 239, 610, 24);
        move(hud_label_, 52, 275, 190, 24);
        move(hud_value_, 248, 275, 610, 24);
        move(safety_label_, 52, 311, 190, 24);
        move(safety_value_, 248, 311, 610, 24);
        move(output_label_, 52, 347, 190, 24);
        move(output_value_, 248, 347, 610, 24);
        move(feedback_, 34, 407, 540, 24);
        move(save_button_, 34, 443, 146, 38);
        move(edit_button_, 190, 443, 146, 38);
        move(restart_button_, 346, 443, 146, 38);
        move(diagnostics_button_, 502, 443, 190, 38);
        move(close_button_, 742, 443, 146, 38);
        move(version_, 650, 409, 238, 22);
''',
    '''        move(title_, 32, 24, 896, 38);
        move(subtitle_, 34, 61, 892, 24);
        move(url_label_, 34, 105, 300, 22);
        move(url_edit_, 34, 132, 892, 32);
        move(provider_value_, 36, 170, 888, 24);
        move(status_group_, 28, 207, 904, 226);
        move(obs_label_, 52, 239, 190, 24);
        move(obs_value_, 248, 239, 650, 24);
        move(hud_label_, 52, 275, 190, 24);
        move(hud_value_, 248, 275, 650, 24);
        move(safety_label_, 52, 311, 190, 24);
        move(safety_value_, 248, 311, 650, 24);
        move(output_label_, 52, 347, 190, 24);
        move(output_value_, 248, 347, 650, 24);
        move(recovery_label_, 52, 383, 190, 24);
        move(recovery_value_, 248, 383, 650, 24);
        move(feedback_, 34, 443, 570, 24);
        move(save_button_, 34, 479, 142, 38);
        move(edit_button_, 186, 479, 142, 38);
        move(restart_button_, 338, 479, 142, 38);
        move(diagnostics_button_, 490, 479, 198, 38);
        move(close_button_, 806, 479, 120, 38);
        move(version_, 680, 445, 246, 22);
''',
)
replace_once(
    "src/config/main.cpp",
    '''    HWND output_label_ = nullptr;
    HWND output_value_ = nullptr;
    HWND feedback_ = nullptr;
''',
    '''    HWND output_label_ = nullptr;
    HWND output_value_ = nullptr;
    HWND recovery_label_ = nullptr;
    HWND recovery_value_ = nullptr;
    HWND feedback_ = nullptr;
''',
)
replace_once(
    "src/config/main.cpp",
    '''    COLORREF safety_color_ = kColorMuted;
    COLORREF output_color_ = kColorMuted;
    COLORREF feedback_color_ = kColorMuted;
''',
    '''    COLORREF safety_color_ = kColorMuted;
    COLORREF output_color_ = kColorMuted;
    COLORREF recovery_color_ = kColorMuted;
    COLORREF feedback_color_ = kColorMuted;
''',
)

replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''#include "common/diagnostic-redaction.hpp"
#include "common/win32-handle.hpp"
''',
    '''#include "common/diagnostic-redaction.hpp"
#include "common/runtime-history-store.hpp"
#include "common/win32-handle.hpp"
''',
)
replace_between(
    "src/diagnostics/diagnostics-exporter.cpp",
    "void append_runtime_summary(\n",
    "struct ProcessSummary {\n",
    r'''std::wstring runtime_restart_reason_name(RuntimeRestartReason reason)
{
    switch (reason) {
    case RuntimeRestartReason::LaunchFailure:
        return L"HUD launch failed";
    case RuntimeRestartReason::StartupTimeout:
        return L"HUD startup timed out";
    case RuntimeRestartReason::WebViewFailure:
        return L"WebView host failed";
    case RuntimeRestartReason::NavigationFailure:
        return L"Chat navigation failed";
    case RuntimeRestartReason::CaptureExclusionFailure:
        return L"Capture exclusion lost";
    case RuntimeRestartReason::ReadySignalFailure:
        return L"HUD readiness signal failed";
    case RuntimeRestartReason::PlacementFailure:
        return L"HUD placement failed";
    case RuntimeRestartReason::PageHealthTimeout:
        return L"Page-health heartbeat timed out";
    case RuntimeRestartReason::ConnectionRecovery:
        return L"Chat connection recovery failed";
    case RuntimeRestartReason::SystemLifecycleRecovery:
        return L"Windows resume recovery";
    case RuntimeRestartReason::UnexpectedExit:
        return L"Unexpected HUD exit";
    case RuntimeRestartReason::ManualRestart:
        return L"Manual restart";
    case RuntimeRestartReason::None:
    default:
        return L"Unavailable";
    }
}

std::wstring format_filetime_utc(std::uint64_t value)
{
    if (value == 0U) {
        return L"Unavailable";
    }

    ULARGE_INTEGER converted{};
    converted.QuadPart = value;
    FILETIME filetime{
        converted.LowPart,
        converted.HighPart,
    };
    SYSTEMTIME utc{};
    if (!FileTimeToSystemTime(&filetime, &utc)) {
        return L"Unavailable";
    }

    wchar_t text[32]{};
    swprintf_s(
        text,
        L"%04u-%02u-%02u %02u:%02u:%02uZ",
        static_cast<unsigned int>(utc.wYear),
        static_cast<unsigned int>(utc.wMonth),
        static_cast<unsigned int>(utc.wDay),
        static_cast<unsigned int>(utc.wHour),
        static_cast<unsigned int>(utc.wMinute),
        static_cast<unsigned int>(utc.wSecond));
    return text;
}

void append_runtime_history(
    std::wostringstream &summary,
    const DiagnosticsRuntimeSnapshot &runtime)
{
    RuntimeTelemetrySnapshot telemetry;
    std::wstring source = L"Unavailable";

    if (runtime.control_status_available &&
        is_valid_control_status_snapshot(runtime.control_status) &&
        has_runtime_telemetry_flag(
            runtime.control_status.runtime_telemetry,
            RuntimeTelemetryHistoryValid)) {
        telemetry = runtime.control_status.runtime_telemetry;
        source = L"Live OBS status";
    } else {
        RuntimeHistoryStore store;
        if (store.load(telemetry)) {
            source = L"Persisted local history";
        }
    }

    summary << L"\nHUD restart history\n"
            << L"-------------------\n"
            << L"Runtime history source: " << source << L"\n";
    if (!has_runtime_telemetry_flag(
            telemetry, RuntimeTelemetryHistoryValid)) {
        summary << L"Last restart reason: Unavailable\n"
                << L"Last HUD exit code: Unavailable\n"
                << L"Last restart automatic: Unavailable\n"
                << L"Consecutive failures: 0\n"
                << L"Automatic restart circuit: Closed\n"
                << L"Last runtime event UTC: Unavailable\n";
        return;
    }

    summary << L"Last restart reason: "
            << runtime_restart_reason_name(telemetry.restart_reason)
            << L"\n"
            << L"Last HUD exit code: ";
    if (telemetry.last_exit_code == kRuntimeExitCodeUnavailable) {
        summary << L"Unavailable\n";
    } else {
        summary << telemetry.last_exit_code << L"\n";
    }
    summary << L"Last restart automatic: "
            << yes_no(has_runtime_telemetry_flag(
                   telemetry, RuntimeTelemetryAutomatic))
            << L"\n"
            << L"Consecutive failures: "
            << telemetry.consecutive_failures << L"\n"
            << L"Automatic restart circuit: "
            << (has_runtime_telemetry_flag(
                    telemetry, RuntimeTelemetryCircuitOpen)
                    ? L"Open"
                    : L"Closed")
            << L"\n"
            << L"Last runtime event UTC: "
            << format_filetime_utc(telemetry.event_filetime_utc)
            << L"\n";
}

void append_runtime_summary(
    std::wostringstream &summary,
    const DiagnosticsRuntimeSnapshot &runtime)
{
    summary << L"\nLive runtime snapshot\n"
            << L"---------------------\n"
            << L"OBS bridge connected: " << yes_no(runtime.obs_connected)
            << L"\n";

    if (runtime.control_status_available &&
        is_valid_control_status_snapshot(runtime.control_status)) {
        const ControlStatusSnapshot &status = runtime.control_status;
        summary << L"OBS output: " << output_description(status) << L"\n"
                << L"Display Capture interlock: "
                << (has_control_status_flag(status, ControlStatusCaptureRisk)
                        ? L"Active"
                        : L"Inactive")
                << L"\n"
                << L"HUD running: "
                << yes_no(has_control_status_flag(
                       status, ControlStatusHudRunning))
                << L"\n"
                << L"HUD visible: "
                << yes_no(has_control_status_flag(
                       status, ControlStatusHudVisible))
                << L"\n";
    } else {
        summary << L"OBS output: Unavailable\n"
                << L"Display Capture interlock: Unavailable\n"
                << L"HUD running: Unavailable\n"
                << L"HUD visible: Unavailable\n";
    }

    if (runtime.hud_health_available &&
        is_valid_hud_health(runtime.hud_health)) {
        summary << L"HUD provider: "
                << provider_name(runtime.hud_health.provider) << L"\n"
                << L"HUD page state: "
                << page_state_name(runtime.hud_health.state) << L"\n"
                << L"HUD detail code: "
                << runtime.hud_health.detail_code << L"\n"
                << L"Current recovery condition: "
                << recovery_condition(runtime.hud_health.state) << L"\n";
    } else {
        summary << L"HUD provider: Unavailable\n"
                << L"HUD page state: Unavailable\n"
                << L"HUD detail code: Unavailable\n"
                << L"Current recovery condition: Unavailable\n";
    }

    append_runtime_history(summary, runtime);
}

''',
)

replace_once(
    "tests/diagnostics-exporter-test.cpp",
    '''#include "diagnostics/diagnostics-exporter.hpp"
''',
    '''#include "common/runtime-history-store.hpp"
#include "diagnostics/diagnostics-exporter.hpp"
''',
)
replace_once(
    "tests/diagnostics-exporter-test.cpp",
    '''    chatview::DiagnosticsRuntimeSnapshot runtime;
''',
    '''    chatview::RuntimeHistoryStore history_store(
        local_app_data / L"ChatView");
    const chatview::RuntimeTelemetrySnapshot persisted_history{
        20U,
        chatview::RuntimeRestartReason::ConnectionRecovery,
        chatview::kMaximumRuntimeFailureCount,
        chatview::RuntimeTelemetryHistoryValid |
            chatview::RuntimeTelemetryAutomatic |
            chatview::RuntimeTelemetryCircuitOpen,
        133485408000000000ULL,
    };
    if (!history_store.save(persisted_history)) {
        std::filesystem::remove_all(root, error);
        return fail("Failed to prepare persisted runtime history");
    }

    chatview::DiagnosticsRuntimeSnapshot runtime;
''',
)
replace_once(
    "tests/diagnostics-exporter-test.cpp",
    '''    runtime.control_status.generation = 1U;
    runtime.hud_health_available = true;
''',
    '''    runtime.control_status.generation = 1U;
    runtime.control_status.runtime_telemetry = {
        19U,
        chatview::RuntimeRestartReason::PageHealthTimeout,
        2U,
        chatview::RuntimeTelemetryHistoryValid |
            chatview::RuntimeTelemetryAutomatic,
        133485408000000001ULL,
    };
    runtime.hud_health_available = true;
''',
)
replace_once(
    "tests/diagnostics-exporter-test.cpp",
    '''        summary.find(
            "Current recovery condition: Provider chat connection lost") ==
            std::string::npos) {
''',
    '''        summary.find(
            "Current recovery condition: Provider chat connection lost") ==
            std::string::npos ||
        summary.find("Runtime history source: Live OBS status") ==
            std::string::npos ||
        summary.find(
            "Last restart reason: Page-health heartbeat timed out") ==
            std::string::npos ||
        summary.find("Last HUD exit code: 19") == std::string::npos ||
        summary.find("Consecutive failures: 2") == std::string::npos ||
        summary.find("Automatic restart circuit: Closed") ==
            std::string::npos) {
''',
)
replace_once(
    "tests/diagnostics-exporter-test.cpp",
    '''    std::filesystem::remove_all(root, error);
    if (error) {
''',
    '''    const chatview::DiagnosticsExportResult persisted_result =
        chatview::export_diagnostics_bundle_to(output_root);
    if (!persisted_result.success || persisted_result.directory.empty()) {
        std::filesystem::remove_all(root, error);
        return fail("Persisted runtime history was not exportable");
    }
    const std::string persisted_summary = read_bytes(
        persisted_result.directory / L"summary.txt");
    if (persisted_summary.find(
            "Runtime history source: Persisted local history") ==
            std::string::npos ||
        persisted_summary.find(
            "Last restart reason: Chat connection recovery failed") ==
            std::string::npos ||
        persisted_summary.find("Last HUD exit code: 20") ==
            std::string::npos ||
        persisted_summary.find("Consecutive failures: 6") ==
            std::string::npos ||
        persisted_summary.find("Automatic restart circuit: Open") ==
            std::string::npos) {
        std::filesystem::remove_all(root, error);
        return fail("Persisted runtime history was not reported correctly");
    }

    std::filesystem::remove_all(root, error);
    if (error) {
''',
)
