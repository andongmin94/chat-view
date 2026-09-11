// SPDX-License-Identifier: GPL-2.0-or-later

#include "diagnostics/diagnostics-exporter.hpp"

#include "common/chat-config.hpp"
#include "common/diagnostic-redaction.hpp"
#include "common/win32-handle.hpp"

#include <Windows.h>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#define CHATVIEW_WIDEN_INNER(value) L##value
#define CHATVIEW_WIDEN(value) CHATVIEW_WIDEN_INNER(value)

namespace chatview {
namespace {

constexpr std::uintmax_t kMaximumLogBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumDiagnosticLines = 1000U;

struct RtlOsVersionInfo {
    ULONG size = sizeof(RtlOsVersionInfo);
    ULONG major = 0U;
    ULONG minor = 0U;
    ULONG build = 0U;
    ULONG platform = 0U;
    WCHAR service_pack[128]{};
};

bool nt_success(LONG status) noexcept
{
    return status >= 0;
}

std::wstring environment_value(const wchar_t *name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0U);
    if (required == 0U) {
        return {};
    }

    std::wstring value(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(
        name, value.data(), static_cast<DWORD>(value.size()));
    if (copied == 0U || copied >= value.size()) {
        return {};
    }
    value.resize(copied);
    return value;
}

std::wstring widen_utf8(std::string_view value)
{
    if (value.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::wstring(value.begin(), value.end());
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required);
    return result;
}

std::string narrow_utf8(std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required,
        nullptr,
        nullptr);
    return result;
}

bool write_utf8_file(
    const std::filesystem::path &path,
    std::wstring_view content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }

    const std::string utf8 = narrow_utf8(content);
    stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return stream.good();
}

std::filesystem::path executable_directory()
{
    std::array<wchar_t, 32768U> path{};
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0U || length >= path.size()) {
        return {};
    }
    return std::filesystem::path(path.data()).parent_path();
}

std::filesystem::path desktop_directory()
{
    PWSTR raw_path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(
        FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &raw_path);
    if (FAILED(result) || raw_path == nullptr) {
        CoTaskMemFree(raw_path);
        return {};
    }

    const std::filesystem::path path(raw_path);
    CoTaskMemFree(raw_path);
    return path;
}

std::filesystem::path create_export_directory(
    const std::filesystem::path &output_root)
{
    if (output_root.empty()) {
        return {};
    }

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t name[96]{};
    swprintf_s(
        name,
        L"ChatView-Diagnostics-%04u%02u%02u-%02u%02u%02u",
        static_cast<unsigned int>(time.wYear),
        static_cast<unsigned int>(time.wMonth),
        static_cast<unsigned int>(time.wDay),
        static_cast<unsigned int>(time.wHour),
        static_cast<unsigned int>(time.wMinute),
        static_cast<unsigned int>(time.wSecond));

    for (unsigned int suffix = 0U; suffix < 100U; ++suffix) {
        std::wstring candidate_name(name);
        if (suffix != 0U) {
            candidate_name.append(L"-");
            candidate_name.append(std::to_wstring(suffix + 1U));
        }

        const std::filesystem::path candidate = output_root / candidate_name;
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) {
            return candidate;
        }
        if (error && error != std::errc::file_exists) {
            return {};
        }
    }
    return {};
}

std::wstring windows_version()
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return L"Unavailable";
    }

    using RtlGetVersionFunction = LONG(WINAPI *)(RtlOsVersionInfo *);
    const auto rtl_get_version =
        reinterpret_cast<RtlGetVersionFunction>(
            GetProcAddress(ntdll, "RtlGetVersion"));
    if (rtl_get_version == nullptr) {
        return L"Unavailable";
    }

    RtlOsVersionInfo version;
    if (!nt_success(rtl_get_version(&version))) {
        return L"Unavailable";
    }

    return std::to_wstring(version.major) + L"." +
           std::to_wstring(version.minor) + L"." +
           std::to_wstring(version.build);
}

std::wstring native_architecture() noexcept
{
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        return L"x64";
    case PROCESSOR_ARCHITECTURE_ARM64:
        return L"ARM64";
    case PROCESSOR_ARCHITECTURE_INTEL:
        return L"x86";
    default:
        return L"Unknown";
    }
}

std::wstring file_version(const std::filesystem::path &path)
{
    DWORD unused = 0U;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &unused);
    if (size == 0U) {
        return L"Unavailable";
    }

    std::vector<std::byte> buffer(size);
    if (!GetFileVersionInfoW(
            path.c_str(), 0U, size, buffer.data())) {
        return L"Unavailable";
    }

    VS_FIXEDFILEINFO *version = nullptr;
    UINT version_size = 0U;
    if (!VerQueryValueW(
            buffer.data(),
            L"\\",
            reinterpret_cast<void **>(&version),
            &version_size) ||
        version == nullptr || version_size < sizeof(VS_FIXEDFILEINFO) ||
        version->dwSignature != 0xfeef04bdU) {
        return L"Unavailable";
    }

    return std::to_wstring(HIWORD(version->dwFileVersionMS)) + L"." +
           std::to_wstring(LOWORD(version->dwFileVersionMS)) + L"." +
           std::to_wstring(HIWORD(version->dwFileVersionLS)) + L"." +
           std::to_wstring(LOWORD(version->dwFileVersionLS));
}

std::wstring sha256_file(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return L"Unavailable";
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> hash_object;
    std::vector<UCHAR> digest;
    std::wstring result = L"Unavailable";

    if (!nt_success(BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0U))) {
        return result;
    }

    DWORD object_size = 0U;
    DWORD digest_size = 0U;
    DWORD copied = 0U;
    if (!nt_success(BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size),
            sizeof(object_size),
            &copied,
            0U)) ||
        !nt_success(BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&digest_size),
            sizeof(digest_size),
            &copied,
            0U)) ||
        object_size == 0U || digest_size == 0U) {
        BCryptCloseAlgorithmProvider(algorithm, 0U);
        return result;
    }

    hash_object.resize(object_size);
    digest.resize(digest_size);
    if (!nt_success(BCryptCreateHash(
            algorithm,
            &hash,
            hash_object.data(),
            static_cast<ULONG>(hash_object.size()),
            nullptr,
            0U,
            0U))) {
        BCryptCloseAlgorithmProvider(algorithm, 0U);
        return result;
    }

    std::array<char, 64U * 1024U> chunk{};
    bool failed = false;
    while (stream) {
        stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize count = stream.gcount();
        if (count > 0 &&
            !nt_success(BCryptHashData(
                hash,
                reinterpret_cast<PUCHAR>(chunk.data()),
                static_cast<ULONG>(count),
                0U))) {
            failed = true;
            break;
        }
    }

    if (!failed && stream.eof() &&
        nt_success(BCryptFinishHash(
            hash,
            digest.data(),
            static_cast<ULONG>(digest.size()),
            0U))) {
        std::wostringstream output;
        output << std::hex << std::setfill(L'0');
        for (const UCHAR byte : digest) {
            output << std::setw(2) << static_cast<unsigned int>(byte);
        }
        result = output.str();
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0U);
    return result;
}

std::wstring provider_name(const std::wstring &url) noexcept
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
    summary << L"
Live runtime snapshot
"
            << L"---------------------
"
            << L"OBS bridge connected: " << yes_no(runtime.obs_connected)
            << L"
";

    if (runtime.control_status_available &&
        is_valid_control_status_snapshot(runtime.control_status)) {
        const ControlStatusSnapshot &status = runtime.control_status;
        summary << L"OBS output: " << output_description(status) << L"
"
                << L"Display Capture interlock: "
                << (has_control_status_flag(status, ControlStatusCaptureRisk)
                        ? L"Active"
                        : L"Inactive")
                << L"
"
                << L"HUD running: "
                << yes_no(has_control_status_flag(
                       status, ControlStatusHudRunning))
                << L"
"
                << L"HUD visible: "
                << yes_no(has_control_status_flag(
                       status, ControlStatusHudVisible))
                << L"
";
    } else {
        summary << L"OBS output: Unavailable
"
                << L"Display Capture interlock: Unavailable
"
                << L"HUD running: Unavailable
"
                << L"HUD visible: Unavailable
";
    }

    if (runtime.hud_health_available &&
        is_valid_hud_health(runtime.hud_health)) {
        summary << L"HUD provider: "
                << provider_name(runtime.hud_health.provider) << L"
"
                << L"HUD page state: "
                << page_state_name(runtime.hud_health.state) << L"
"
                << L"HUD detail code: "
                << runtime.hud_health.detail_code << L"
"
                << L"Current recovery condition: "
                << recovery_condition(runtime.hud_health.state) << L"
";
    } else {
        summary << L"HUD provider: Unavailable
"
                << L"HUD page state: Unavailable
"
                << L"HUD detail code: Unavailable
"
                << L"Current recovery condition: Unavailable
";
    }
}

struct ProcessSummary {
    unsigned int hud_count = 0U;
    unsigned int webview_count = 0U;
    std::wstring webview_version = L"Unavailable";
};

ProcessSummary running_process_summary() noexcept
{
    ProcessSummary summary;
    const HANDLE raw_snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U);
    if (raw_snapshot == INVALID_HANDLE_VALUE) {
        return summary;
    }
    UniqueHandle snapshot(raw_snapshot);

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        return summary;
    }

    do {
        if (_wcsicmp(entry.szExeFile, L"chat-view-hud.exe") == 0) {
            ++summary.hud_count;
            continue;
        }
        if (_wcsicmp(entry.szExeFile, L"msedgewebview2.exe") != 0) {
            continue;
        }

        ++summary.webview_count;
        if (summary.webview_version != L"Unavailable") {
            continue;
        }

        UniqueHandle process(OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            entry.th32ProcessID));
        if (!process) {
            continue;
        }

        std::array<wchar_t, 32768U> image_path{};
        DWORD length = static_cast<DWORD>(image_path.size());
        if (QueryFullProcessImageNameW(
                process.get(), 0U, image_path.data(), &length)) {
            summary.webview_version =
                file_version(std::filesystem::path(image_path.data()));
        }
    } while (Process32NextW(snapshot.get(), &entry));

    return summary;
}

std::wstring read_log_tail(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    if (end < 0) {
        return {};
    }

    const std::streamoff maximum =
        static_cast<std::streamoff>(kMaximumLogBytes);
    const std::streamoff start = std::max<std::streamoff>(0, end - maximum);
    stream.seekg(start, std::ios::beg);

    std::string bytes(
        static_cast<std::size_t>(end - start), '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(stream.gcount()));
    if (start != 0) {
        const std::size_t newline = bytes.find('\n');
        if (newline != std::string::npos) {
            bytes.erase(0U, newline + 1U);
        }
    }
    return widen_utf8(bytes);
}

std::vector<std::filesystem::path> latest_obs_logs()
{
    const std::wstring roaming = environment_value(L"APPDATA");
    if (roaming.empty()) {
        return {};
    }

    const std::filesystem::path directory =
        std::filesystem::path(roaming) / L"obs-studio" / L"logs";
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return {};
    }

    std::vector<std::filesystem::path> logs;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (iterator->is_regular_file(error)) {
            logs.push_back(iterator->path());
        }
    }

    std::sort(
        logs.begin(),
        logs.end(),
        [](const std::filesystem::path &left,
           const std::filesystem::path &right) {
            std::error_code left_error;
            std::error_code right_error;
            const auto left_time =
                std::filesystem::last_write_time(left, left_error);
            const auto right_time =
                std::filesystem::last_write_time(right, right_error);
            if (left_error || right_error) {
                return left.filename().wstring() > right.filename().wstring();
            }
            return left_time > right_time;
        });
    if (logs.size() > 3U) {
        logs.resize(3U);
    }
    return logs;
}

std::wstring filtered_obs_log(
    const DiagnosticRedactionContext &redaction)
{
    std::vector<std::wstring> lines;
    for (const std::filesystem::path &path : latest_obs_logs()) {
        const std::wstring content = read_log_tail(path);
        std::size_t start = 0U;
        while (start <= content.size()) {
            const std::size_t end = content.find(L'\n', start);
            const std::size_t length =
                end == std::wstring::npos
                    ? content.size() - start
                    : end - start;
            std::wstring line = content.substr(start, length);
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            if (line.find(L"[ChatView OBS]") != std::wstring::npos) {
                lines.push_back(redact_diagnostic_text(line, redaction));
                if (lines.size() > kMaximumDiagnosticLines) {
                    lines.erase(lines.begin());
                }
            }
            if (end == std::wstring::npos) {
                break;
            }
            start = end + 1U;
        }
    }

    if (lines.empty()) {
        return L"No ChatView entries were found in the three newest OBS logs.\n";
    }

    std::wstring output;
    for (const std::wstring &line : lines) {
        output.append(line);
        output.push_back(L'\n');
    }
    return output;
}

void append_file_summary(
    std::wostringstream &summary,
    const std::filesystem::path &path,
    const wchar_t *label)
{
    std::error_code error;
    const bool exists = std::filesystem::is_regular_file(path, error);
    summary << L"- " << label << L": ";
    if (!exists) {
        summary << L"missing\n";
        return;
    }

    const std::uintmax_t size = std::filesystem::file_size(path, error);
    summary << L"version " << file_version(path)
            << L", bytes " << (error ? 0U : size)
            << L", sha256 " << sha256_file(path) << L"\n";
}

} // namespace

DiagnosticsExportResult export_diagnostics_bundle_to(
    const std::filesystem::path &output_root,
    const DiagnosticsRuntimeSnapshot &runtime) noexcept
{
    DiagnosticsExportResult result;
    try {
        result.directory = create_export_directory(output_root);
        if (result.directory.empty()) {
            result.error = L"The Desktop diagnostics folder could not be created.";
            return result;
        }

        const DiagnosticRedactionContext redaction{
            environment_value(L"USERPROFILE"),
            environment_value(L"LOCALAPPDATA"),
            environment_value(L"APPDATA"),
        };

        ChatConfig config;
        const bool configured = load_chat_config(config);
        const ProcessSummary processes = running_process_summary();
        const std::filesystem::path binary_directory = executable_directory();
        const std::filesystem::path obs_root =
            binary_directory.parent_path().parent_path();

        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        std::wostringstream summary;
        summary << L"ChatView diagnostics\n"
                << L"====================\n"
                << L"Created UTC: "
                << std::setfill(L'0')
                << std::setw(4) << utc.wYear << L"-"
                << std::setw(2) << utc.wMonth << L"-"
                << std::setw(2) << utc.wDay << L" "
                << std::setw(2) << utc.wHour << L":"
                << std::setw(2) << utc.wMinute << L":"
                << std::setw(2) << utc.wSecond << L"\n"
                << L"ChatView version: " CHATVIEW_WIDEN(CHATVIEW_VERSION) L"\n"
                << L"Windows version: " << windows_version() << L"\n"
                << L"Native architecture: " << native_architecture() << L"\n"
                << L"Configured provider: "
                << (configured ? provider_name(config.url) : L"Not configured")
                << L"\n"
                << L"ChatView HUD process count: " << processes.hud_count << L"\n"
                << L"WebView2 process count: " << processes.webview_count << L"\n"
                << L"WebView2 runtime version: "
                << processes.webview_version << L"\n\n"
                << L"Installed binaries\n"
                << L"------------------\n";

        append_file_summary(
            summary,
            binary_directory / L"chat-view-obs.dll",
            L"chat-view-obs.dll");
        append_file_summary(
            summary,
            binary_directory / L"chat-view-hud.exe",
            L"chat-view-hud.exe");
        append_file_summary(
            summary,
            binary_directory / L"chat-view-config.exe",
            L"chat-view-config.exe");
        append_file_summary(
            summary,
            binary_directory / L"chat-view-diagnostics.exe",
            L"chat-view-diagnostics.exe");
        append_file_summary(
            summary,
            obs_root / L"bin" / L"64bit" / L"obs64.exe",
            L"obs64.exe");
        append_runtime_summary(summary, runtime);

        const std::wstring privacy =
            L"ChatView diagnostics privacy rules\n"
            L"=================================\n"
            L"- No chat messages are collected.\n"
            L"- The configured chat or broadcast URL is not exported.\n"
            L"- Only the recognized provider name is exported.\n"
            L"- OBS logs are filtered to lines containing [ChatView OBS].\n"
            L"- URLs, user-profile paths, IPC object names, and IPC command-line "
            L"values are redacted.\n"
            L"- Nothing is uploaded automatically. Review the folder before "
            L"sharing it.\n";

        if (!write_utf8_file(
                result.directory / L"summary.txt",
                redact_diagnostic_text(summary.str(), redaction)) ||
            !write_utf8_file(
                result.directory / L"chatview-obs.log",
                filtered_obs_log(redaction)) ||
            !write_utf8_file(
                result.directory / L"privacy.txt",
                privacy)) {
            result.error = L"One or more diagnostics files could not be written.";
            std::error_code cleanup_error;
            std::filesystem::remove_all(result.directory, cleanup_error);
            result.directory.clear();
            return result;
        }

        result.success = true;
        return result;
    } catch (const std::exception &error) {
        result.error = L"Diagnostics export failed: " + widen_utf8(error.what());
    } catch (...) {
        result.error = L"Diagnostics export failed with an unknown error.";
    }

    if (!result.directory.empty()) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(result.directory, cleanup_error);
        result.directory.clear();
    }
    return result;
}

DiagnosticsExportResult export_diagnostics_bundle_to(
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
