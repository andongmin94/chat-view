from pathlib import Path

path = Path("src/diagnostics/diagnostics-exporter.cpp")
text = path.read_text(encoding="utf-8")
start_marker = "void append_runtime_summary("
end_marker = "\nstruct ProcessSummary"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("append_runtime_summary markers were not found")

replacement = r'''void append_runtime_summary(
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
}
'''

updated = text[:start] + replacement + text[end:]
if 'summary << L"\nLive runtime snapshot\n"' not in updated:
    raise SystemExit("escaped diagnostics literals were not installed")
path.write_text(updated, encoding="utf-8", newline="\n")
