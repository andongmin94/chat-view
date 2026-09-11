from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


Path("src/diagnostics/diagnostics-exporter.hpp").write_text(
    '''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/control-status.hpp"
#include "common/hud-health.hpp"

#include <filesystem>
#include <string>

namespace chatview {

struct DiagnosticsRuntimeSnapshot {
    bool obs_connected = false;
    bool control_status_available = false;
    ControlStatusSnapshot control_status;
    bool hud_health_available = false;
    HudHealthSnapshot hud_health;
};

struct DiagnosticsExportResult {
    bool success = false;
    std::filesystem::path directory;
    std::wstring error;
};

[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle() noexcept;
[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle(
    const DiagnosticsRuntimeSnapshot &runtime) noexcept;
[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root) noexcept;
[[nodiscard]] DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root,
    const DiagnosticsRuntimeSnapshot &runtime) noexcept;

} // namespace chatview
''',
    encoding="utf-8",
    newline="\n",
)

replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''std::wstring provider_name(const std::wstring &url) noexcept
{
    if (url.starts_with(L"https://weflab.com/")) {
        return L"Weflab";
    }
    if (url.starts_with(L"https://chzzk.naver.com/")) {
        return L"CHZZK";
    }
    if (url.starts_with(L"https://play.sooplive.com/")) {
        return L"SOOP";
    }
    if (url.starts_with(L"https://www.youtube.com/")) {
        return L"YouTube";
    }
    return L"Unknown";
}

struct ProcessSummary {
''',
    '''std::wstring provider_name(const std::wstring &url) noexcept
{
    if (url.starts_with(L"https://weflab.com/")) {
        return L"Weflab";
    }
    if (url.starts_with(L"https://chzzk.naver.com/")) {
        return L"CHZZK";
    }
    if (url.starts_with(L"https://play.sooplive.com/")) {
        return L"SOOP";
    }
    if (url.starts_with(L"https://www.youtube.com/")) {
        return L"YouTube";
    }
    return L"Unknown";
}

const wchar_t *yes_no(bool value) noexcept
{
    return value ? L"Yes" : L"No";
}

std::wstring provider_name(HudProvider provider)
{
    switch (provider) {
    case HudProvider::Weflab:
        return L"Weflab";
    case HudProvider::Chzzk:
        return L"CHZZK";
    case HudProvider::Soop:
        return L"SOOP";
    case HudProvider::YouTube:
        return L"YouTube";
    case HudProvider::Unknown:
    default:
        return L"Unknown";
    }
}

std::wstring page_state_name(HudPageState state)
{
    switch (state) {
    case HudPageState::Starting:
        return L"Starting";
    case HudPageState::SetupRequired:
        return L"Setup required";
    case HudPageState::Loading:
        return L"Loading";
    case HudPageState::Ready:
        return L"Ready";
    case HudPageState::Retrying:
        return L"Retrying";
    case HudPageState::Recovering:
        return L"Recovering";
    case HudPageState::LoginRequired:
        return L"Login required";
    case HudPageState::Offline:
        return L"Broadcast offline";
    case HudPageState::LayoutChanged:
        return L"Provider layout changed";
    case HudPageState::NetworkOffline:
        return L"Network offline";
    case HudPageState::ConnectionLost:
        return L"Connection lost";
    case HudPageState::SystemPaused:
        return L"Windows session paused";
    case HudPageState::SystemResuming:
        return L"Windows session resuming";
    case HudPageState::Fatal:
        return L"Fatal";
    case HudPageState::Unknown:
    default:
        return L"Unknown";
    }
}

std::wstring recovery_condition(HudPageState state)
{
    switch (state) {
    case HudPageState::Starting:
        return L"HUD startup in progress";
    case HudPageState::Loading:
        return L"Chat page load in progress";
    case HudPageState::Retrying:
        return L"Navigation retry active";
    case HudPageState::Recovering:
        return L"WebView or heartbeat recovery active";
    case HudPageState::NetworkOffline:
        return L"Browser network is offline";
    case HudPageState::ConnectionLost:
        return L"Provider chat connection lost";
    case HudPageState::SystemPaused:
        return L"Windows session is paused";
    case HudPageState::SystemResuming:
        return L"Windows resume revalidation active";
    case HudPageState::Fatal:
        return L"HUD reported a fatal error";
    case HudPageState::SetupRequired:
        return L"Chat configuration required";
    case HudPageState::LoginRequired:
        return L"Provider login required";
    case HudPageState::Offline:
        return L"Broadcast is offline or ended";
    case HudPageState::LayoutChanged:
        return L"Provider page layout changed";
    case HudPageState::Ready:
        return L"None active";
    case HudPageState::Unknown:
    default:
        return L"Unavailable";
    }
}

std::wstring output_description(const ControlStatusSnapshot &snapshot)
{
    std::vector<std::wstring> outputs;
    if (has_control_status_flag(snapshot, ControlStatusStreaming)) {
        outputs.emplace_back(L"Streaming");
    }
    if (has_control_status_flag(snapshot, ControlStatusRecording)) {
        outputs.emplace_back(L"Recording");
    }
    if (has_control_status_flag(snapshot, ControlStatusReplayBuffer)) {
        outputs.emplace_back(L"Replay buffer");
    }
    if (has_control_status_flag(snapshot, ControlStatusVirtualCamera)) {
        outputs.emplace_back(L"Virtual camera");
    }
    if (outputs.empty()) {
        return L"Idle";
    }

    std::wstring result;
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        if (index != 0U) {
            result.append(L", ");
        }
        result.append(outputs[index]);
    }
    return result;
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
}

struct ProcessSummary {
''',
)

replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root) noexcept
{
    DiagnosticsExportResult result;
''',
    '''DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root,
    const DiagnosticsRuntimeSnapshot &runtime) noexcept
{
    DiagnosticsExportResult result;
''',
)
replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''        append_file_summary(
            summary,
            obs_root / L"bin" / L"64bit" / L"obs64.exe",
            L"obs64.exe");

        const std::wstring privacy =
''',
    '''        append_file_summary(
            summary,
            obs_root / L"bin" / L"64bit" / L"obs64.exe",
            L"obs64.exe");
        append_runtime_summary(summary, runtime);

        const std::wstring privacy =
''',
)
replace_once(
    "src/diagnostics/diagnostics-exporter.cpp",
    '''DiagnosticsExportResult export_diagnostics_bundle() noexcept
{
    const std::filesystem::path desktop = desktop_directory();
    if (desktop.empty()) {
        DiagnosticsExportResult result;
        result.error = L"The Desktop diagnostics location could not be resolved.";
        return result;
    }
    return export_diagnostics_bundle_to(desktop);
}

} // namespace chatview
''',
    '''DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root) noexcept
{
    return export_diagnostics_bundle_to(output_root, {});
}

DiagnosticsExportResult export_diagnostics_bundle(
    const DiagnosticsRuntimeSnapshot &runtime) noexcept
{
    const std::filesystem::path desktop = desktop_directory();
    if (desktop.empty()) {
        DiagnosticsExportResult result;
        result.error = L"The Desktop diagnostics location could not be resolved.";
        return result;
    }
    return export_diagnostics_bundle_to(desktop, runtime);
}

DiagnosticsExportResult export_diagnostics_bundle() noexcept
{
    return export_diagnostics_bundle({});
}

} // namespace chatview
''',
)

replace_once(
    "src/config/main.cpp",
    '''#include "config/control-status-reader.hpp"
''',
    '''#include "config/control-status-reader.hpp"
#include "diagnostics/diagnostics-exporter.hpp"
''',
)
replace_once(
    "src/config/main.cpp",
    '''constexpr int kRestartButtonId = 1004;
constexpr int kCloseButtonId = 1005;
constexpr int kWindowWidthDip = 820;
''',
    '''constexpr int kRestartButtonId = 1004;
constexpr int kCloseButtonId = 1005;
constexpr int kDiagnosticsButtonId = 1006;
constexpr int kWindowWidthDip = 920;
''',
)
replace_once(
    "src/config/main.cpp",
    '''            case kRestartButtonId:
                restart_hud();
                return 0L;
            case kCloseButtonId:
''',
    '''            case kRestartButtonId:
                restart_hud();
                return 0L;
            case kDiagnosticsButtonId:
                export_diagnostics();
                return 0L;
            case kCloseButtonId:
''',
)
replace_once(
    "src/config/main.cpp",
    '''        restart_button_ = create_button(
            L"Restart HUD", kRestartButtonId, BS_PUSHBUTTON);
        close_button_ = create_button(
''',
    '''        restart_button_ = create_button(
            L"Restart HUD", kRestartButtonId, BS_PUSHBUTTON);
        diagnostics_button_ = create_button(
            L"Export diagnostics", kDiagnosticsButtonId, BS_PUSHBUTTON);
        close_button_ = create_button(
''',
)
replace_once(
    "src/config/main.cpp",
    '''        const std::array<HWND, 18U> required{
''',
    '''        const std::array<HWND, 19U> required{
''',
)
replace_once(
    "src/config/main.cpp",
    '''            save_button_,
            edit_button_,
            restart_button_};
''',
    '''            save_button_,
            edit_button_,
            restart_button_,
            diagnostics_button_};
''',
)
replace_once(
    "src/config/main.cpp",
    '''        if (!connected_ || !status_reader_.parent_alive()) {
            connected_ = false;
            set_disconnected_status();
''',
    '''        if (!connected_ || !status_reader_.parent_alive()) {
            connected_ = false;
            snapshot_available_ = false;
            latest_health_available_ = false;
            set_disconnected_status();
''',
)
replace_once(
    "src/config/main.cpp",
    '''        if (!status_reader_.read(snapshot)) {
            set_colored_text(
''',
    '''        if (!status_reader_.read(snapshot)) {
            snapshot_available_ = false;
            latest_health_available_ = false;
            set_colored_text(
''',
)
replace_once(
    "src/config/main.cpp",
    '''        const bool status_changed =
            force || snapshot.generation != snapshot_.generation;
        if (status_changed) {
            snapshot_ = snapshot;
        }
''',
    '''        const bool status_changed =
            force || snapshot.generation != snapshot_.generation;
        if (status_changed) {
            snapshot_ = snapshot;
        }
        snapshot_available_ = true;
''',
)
replace_once(
    "src/config/main.cpp",
    '''        const bool health_available =
            query_hud_health(hud, health);
        const HealthPresentation health_status =
''',
    '''        const bool health_available =
            query_hud_health(hud, health);
        latest_health_available_ = health_available;
        if (health_available) {
            latest_health_ = health;
        }
        const HealthPresentation health_status =
''',
)
replace_once(
    "src/config/main.cpp",
    '''    void set_feedback(
        const std::wstring &text, COLORREF color)
''',
    '''    void export_diagnostics()
    {
        chatview::DiagnosticsRuntimeSnapshot runtime;
        runtime.obs_connected =
            connected_ && status_reader_.parent_alive();
        runtime.control_status_available =
            runtime.obs_connected && snapshot_available_;
        runtime.control_status = snapshot_;
        runtime.hud_health_available = latest_health_available_;
        runtime.hud_health = latest_health_;

        const chatview::DiagnosticsExportResult result =
            chatview::export_diagnostics_bundle(runtime);
        if (!result.success) {
            set_feedback(
                result.error.empty()
                    ? L"ChatView diagnostics could not be exported."
                    : result.error,
                kColorError);
            return;
        }

        const HINSTANCE opened = ShellExecuteW(
            window_,
            L"open",
            result.directory.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(opened) <= 32) {
            set_feedback(
                L"Diagnostics were exported, but the folder could not be opened.",
                kColorWarning);
            return;
        }

        set_feedback(
            L"Privacy-filtered diagnostics exported. Review before sharing.",
            kColorGood);
    }

    void set_feedback(
        const std::wstring &text, COLORREF color)
''',
)
replace_once(
    "src/config/main.cpp",
    '''        const std::array<HWND, 19U> body_controls{
''',
    '''        const std::array<HWND, 20U> body_controls{
''',
)
replace_once(
    "src/config/main.cpp",
    '''            save_button_,
            edit_button_,
            restart_button_,
            close_button_,
''',
    '''            save_button_,
            edit_button_,
            restart_button_,
            diagnostics_button_,
            close_button_,
''',
)
replace_once(
    "src/config/main.cpp",
    '''        move(title_, 32, 24, 756, 38);
        move(subtitle_, 34, 61, 752, 24);
        move(url_label_, 34, 105, 300, 22);
        move(url_edit_, 34, 132, 752, 32);
        move(provider_value_, 36, 170, 748, 24);
        move(status_group_, 28, 207, 764, 190);
        move(obs_label_, 52, 239, 190, 24);
        move(obs_value_, 248, 239, 510, 24);
        move(hud_label_, 52, 275, 190, 24);
        move(hud_value_, 248, 275, 510, 24);
        move(safety_label_, 52, 311, 190, 24);
        move(safety_value_, 248, 311, 510, 24);
        move(output_label_, 52, 347, 190, 24);
        move(output_value_, 248, 347, 510, 24);
        move(feedback_, 34, 407, 450, 24);
        move(save_button_, 34, 443, 142, 38);
        move(edit_button_, 188, 443, 142, 38);
        move(restart_button_, 342, 443, 142, 38);
        move(close_button_, 644, 443, 142, 38);
        move(version_, 500, 409, 286, 22);
''',
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
)
replace_once(
    "src/config/main.cpp",
    '''    chatview::ControlStatusSnapshot snapshot_;
    HINSTANCE instance_ = nullptr;
''',
    '''    chatview::ControlStatusSnapshot snapshot_;
    chatview::HudHealthSnapshot latest_health_;
    HINSTANCE instance_ = nullptr;
''',
)
replace_once(
    "src/config/main.cpp",
    '''    HWND restart_button_ = nullptr;
    HWND close_button_ = nullptr;
''',
    '''    HWND restart_button_ = nullptr;
    HWND diagnostics_button_ = nullptr;
    HWND close_button_ = nullptr;
''',
)
replace_once(
    "src/config/main.cpp",
    '''    UINT dpi_ = 96U;
    bool connected_ = false;
''',
    '''    UINT dpi_ = 96U;
    bool connected_ = false;
    bool snapshot_available_ = false;
    bool latest_health_available_ = false;
''',
)

replace_once(
    "CMakeLists.txt",
    '''project(chat-view-obs VERSION 0.3.7 LANGUAGES CXX)
''',
    '''project(chat-view-obs VERSION 0.3.8 LANGUAGES CXX)
''',
)
replace_once(
    "CMakeLists.txt",
    '''chatview_enable_win32(chat-view-diagnostics-core)
chatview_enable_warnings(chat-view-diagnostics-core)

add_executable(chat-view-diagnostics WIN32
''',
    '''chatview_enable_win32(chat-view-diagnostics-core)
chatview_enable_warnings(chat-view-diagnostics-core)
target_link_libraries(
    chat-view-config PRIVATE chat-view-diagnostics-core)

add_executable(chat-view-diagnostics WIN32
''',
)

replace_once(
    "tests/diagnostics-exporter-test.cpp",
    '''    const chatview::DiagnosticsExportResult result =
        chatview::export_diagnostics_bundle_to(output_root);
''',
    '''    chatview::DiagnosticsRuntimeSnapshot runtime;
    runtime.obs_connected = true;
    runtime.control_status_available = true;
    runtime.control_status.flags =
        chatview::ControlStatusStreaming |
        chatview::ControlStatusHudRunning |
        chatview::ControlStatusHudVisible;
    runtime.control_status.hud_process_id = 4242U;
    runtime.control_status.generation = 1U;
    runtime.hud_health_available = true;
    runtime.hud_health = {
        chatview::HudPageState::ConnectionLost,
        chatview::HudProvider::YouTube,
        7U};

    const chatview::DiagnosticsExportResult result =
        chatview::export_diagnostics_bundle_to(output_root, runtime);
''',
)
replace_once(
    "tests/diagnostics-exporter-test.cpp",
    '''        privacy.find("No chat messages are collected") ==
            std::string::npos ||
        summary.find("ChatView diagnostics") == std::string::npos) {
''',
    '''        privacy.find("No chat messages are collected") ==
            std::string::npos ||
        summary.find("ChatView diagnostics") == std::string::npos ||
        summary.find("OBS bridge connected: Yes") == std::string::npos ||
        summary.find("OBS output: Streaming") == std::string::npos ||
        summary.find("Display Capture interlock: Inactive") ==
            std::string::npos ||
        summary.find("HUD provider: YouTube") == std::string::npos ||
        summary.find("HUD page state: Connection lost") ==
            std::string::npos ||
        summary.find(
            "Current recovery condition: Provider chat connection lost") ==
            std::string::npos) {
''',
)

replace_once(
    "README.md",
    '''- Windows suspend, workstation-lock, and cancelled-shutdown recovery;
- OBS **Tools → ChatView Settings...** configuration;
''',
    '''- Windows suspend, workstation-lock, and cancelled-shutdown recovery;
- privacy-filtered diagnostics from OBS or the Control Center, with no automatic upload;
- repeated live HUD/WebView2 resource-growth soak coverage;
- OBS **Tools → ChatView Settings...** configuration;
''',
)
replace_once(
    "README.md",
    '''Saving broadcasts a local configuration-change message, so the running HUD reloads without restarting OBS.
''',
    '''Saving broadcasts a local configuration-change message, so the running HUD reloads without restarting OBS.

Use **Export diagnostics** in the Control Center, or **Tools → Export ChatView Diagnostics...** in OBS, to create a Desktop folder containing a binary-integrity summary, the current OBS/HUD state when available, and only ChatView-tagged OBS log lines. The export omits the configured URL and chat messages, redacts user-profile paths and IPC names, uploads nothing automatically, and must be reviewed before sharing.
''',
)
