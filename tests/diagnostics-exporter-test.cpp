// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/runtime-history-store.hpp"
#include "diagnostics/diagnostics-exporter.hpp"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

std::string read_bytes(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

bool write_bytes(
    const std::filesystem::path &path, const std::string &content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return stream.good();
}

class EnvironmentRestore final {
public:
    explicit EnvironmentRestore(const wchar_t *name) : name_(name)
    {
        const DWORD required = GetEnvironmentVariableW(name_, nullptr, 0U);
        if (required == 0U) {
            return;
        }
        previous_.resize(required, L'\0');
        const DWORD copied = GetEnvironmentVariableW(
            name_, previous_.data(), static_cast<DWORD>(previous_.size()));
        if (copied != 0U && copied < previous_.size()) {
            previous_.resize(copied);
            had_value_ = true;
        }
    }

    ~EnvironmentRestore()
    {
        SetEnvironmentVariableW(
            name_, had_value_ ? previous_.c_str() : nullptr);
    }

    EnvironmentRestore(const EnvironmentRestore &) = delete;
    EnvironmentRestore &operator=(const EnvironmentRestore &) = delete;

private:
    const wchar_t *name_ = nullptr;
    std::wstring previous_;
    bool had_value_ = false;
};

} // namespace

int main()
{
    EnvironmentRestore restore_user_profile(L"USERPROFILE");
    EnvironmentRestore restore_local_app_data(L"LOCALAPPDATA");
    EnvironmentRestore restore_app_data(L"APPDATA");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (L"chatview-diagnostics-test-" +
         std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path user_profile = root / L"PrivateUser";
    const std::filesystem::path local_app_data =
        user_profile / L"AppData" / L"Local";
    const std::filesystem::path app_data =
        user_profile / L"AppData" / L"Roaming";
    const std::filesystem::path log_directory =
        app_data / L"obs-studio" / L"logs";
    const std::filesystem::path output_root = root / L"Output";

    std::error_code error;
    std::filesystem::create_directories(log_directory, error);
    std::filesystem::create_directories(output_root, error);
    if (error ||
        !SetEnvironmentVariableW(L"USERPROFILE", user_profile.c_str()) ||
        !SetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.c_str()) ||
        !SetEnvironmentVariableW(L"APPDATA", app_data.c_str())) {
        std::filesystem::remove_all(root, error);
        return fail("Failed to prepare the diagnostics test environment");
    }

    const std::string secret_url =
        "https://www.youtube.com/watch?v=private-video-token";
    const std::string secret_ipc =
        "Local\\ChatViewOBS.State.123.0123456789abcdef";
    const std::string secret_chat = "viewer: this is a private chat message";
    const std::string log =
        "12:00:00.000: [ChatView OBS] opened " + secret_url +
        " from " + local_app_data.string() +
        " --mapping \"" + secret_ipc + "\"\n" +
        "12:00:00.001: " + secret_chat + "\n" +
        "12:00:00.002: [ChatView OBS] HUD runtime started\n";
    if (!write_bytes(log_directory / L"2026-09-11 12-00-00.txt", log)) {
        std::filesystem::remove_all(root, error);
        return fail("Failed to write the diagnostics test log");
    }

    chatview::RuntimeHistoryStore history_store(
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
    runtime.obs_connected = true;
    runtime.control_status_available = true;
    runtime.control_status.flags =
        chatview::ControlStatusStreaming |
        chatview::ControlStatusHudRunning |
        chatview::ControlStatusHudVisible;
    runtime.control_status.hud_process_id = 4242U;
    runtime.control_status.generation = 1U;
    runtime.control_status.runtime_telemetry = {
        19U,
        chatview::RuntimeRestartReason::PageHealthTimeout,
        2U,
        chatview::RuntimeTelemetryHistoryValid |
            chatview::RuntimeTelemetryAutomatic,
        133485408000000001ULL,
    };
    runtime.hud_health_available = true;
    runtime.hud_health = {
        chatview::HudPageState::ConnectionLost,
        chatview::HudProvider::YouTube,
        7U};

    const chatview::DiagnosticsExportResult result =
        chatview::export_diagnostics_bundle_to(output_root, runtime);
    if (!result.success || result.directory.empty()) {
        std::wcerr << L"Diagnostics export failed: " << result.error << L'\n';
        std::filesystem::remove_all(root, error);
        return 1;
    }

    const std::filesystem::path summary_path =
        result.directory / L"summary.txt";
    const std::filesystem::path log_path =
        result.directory / L"chatview-obs.log";
    const std::filesystem::path privacy_path =
        result.directory / L"privacy.txt";
    if (!std::filesystem::is_regular_file(summary_path) ||
        !std::filesystem::is_regular_file(log_path) ||
        !std::filesystem::is_regular_file(privacy_path)) {
        std::filesystem::remove_all(root, error);
        return fail("Diagnostics export omitted a required file");
    }

    const std::string summary = read_bytes(summary_path);
    const std::string filtered_log = read_bytes(log_path);
    const std::string privacy = read_bytes(privacy_path);
    const std::string private_profile = user_profile.string();

    if (summary.find(secret_url) != std::string::npos ||
        summary.find(private_profile) != std::string::npos ||
        filtered_log.find(secret_url) != std::string::npos ||
        filtered_log.find("private-video-token") != std::string::npos ||
        filtered_log.find("0123456789abcdef") != std::string::npos ||
        filtered_log.find(private_profile) != std::string::npos ||
        filtered_log.find(secret_chat) != std::string::npos) {
        std::filesystem::remove_all(root, error);
        return fail("Diagnostics export leaked a private value");
    }

    if (filtered_log.find("<url-redacted>") == std::string::npos ||
        filtered_log.find("<redacted>") == std::string::npos ||
        filtered_log.find("[ChatView OBS] HUD runtime started") ==
            std::string::npos ||
        privacy.find("No chat messages are collected") ==
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
        std::filesystem::remove_all(root, error);
        return fail("Diagnostics export lost its expected safe content");
    }

    const chatview::DiagnosticsExportResult persisted_result =
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
        return fail("Failed to clean up the diagnostics test directory");
    }
    return 0;
}
