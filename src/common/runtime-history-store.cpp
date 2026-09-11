// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/runtime-history-store.hpp"

#include "common/win32-handle.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace chatview {
namespace {

constexpr wchar_t kRuntimeHistoryFileName[] = L"runtime-history-v1.bin";
constexpr std::uint32_t kRuntimeHistoryMagic = 0x48525643U; // "CVRH"
constexpr std::uint32_t kRuntimeHistoryVersion = 1U;
constexpr std::uint32_t kFnvOffsetBasis = 2166136261U;
constexpr std::uint32_t kFnvPrime = 16777619U;

struct RuntimeHistoryRecord {
    std::uint32_t magic = kRuntimeHistoryMagic;
    std::uint32_t version = kRuntimeHistoryVersion;
    std::uint32_t size = 64U;
    std::uint32_t checksum = 0U;
    std::uint32_t last_exit_code = kRuntimeExitCodeUnavailable;
    std::uint32_t restart_reason = 0U;
    std::uint32_t consecutive_failures = 0U;
    std::uint32_t flags = RuntimeTelemetryNone;
    std::uint64_t event_filetime_utc = 0U;
    std::array<std::uint8_t, 24U> reserved{};
};

static_assert(sizeof(RuntimeHistoryRecord) == 64U);

std::filesystem::path default_runtime_history_file() noexcept
{
    try {
        const DWORD required =
            GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0U);
        if (required == 0U) {
            return {};
        }

        std::wstring value(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(
            L"LOCALAPPDATA", value.data(), required);
        if (written == 0U || written >= required) {
            return {};
        }
        value.resize(written);
        return std::filesystem::path(value) / L"ChatView" /
               kRuntimeHistoryFileName;
    } catch (...) {
        return {};
    }
}

std::uint32_t record_checksum(
    const RuntimeHistoryRecord &record) noexcept
{
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(&record);
    constexpr std::size_t checksum_begin =
        offsetof(RuntimeHistoryRecord, checksum);
    constexpr std::size_t checksum_end =
        checksum_begin + sizeof(std::uint32_t);

    std::uint32_t checksum = kFnvOffsetBasis;
    for (std::size_t index = 0U;
         index < sizeof(RuntimeHistoryRecord);
         ++index) {
        if (index >= checksum_begin && index < checksum_end) {
            continue;
        }
        checksum ^= bytes[index];
        checksum *= kFnvPrime;
    }
    return checksum;
}

RuntimeHistoryRecord make_record(
    const RuntimeTelemetrySnapshot &snapshot) noexcept
{
    RuntimeHistoryRecord record;
    record.last_exit_code = snapshot.last_exit_code;
    record.restart_reason =
        static_cast<std::uint32_t>(snapshot.restart_reason);
    record.consecutive_failures = snapshot.consecutive_failures;
    record.flags = snapshot.flags;
    record.event_filetime_utc = snapshot.event_filetime_utc;
    record.checksum = record_checksum(record);
    return record;
}

bool is_valid_record(const RuntimeHistoryRecord &record) noexcept
{
    if (record.magic != kRuntimeHistoryMagic ||
        record.version != kRuntimeHistoryVersion ||
        record.size != sizeof(RuntimeHistoryRecord) ||
        record.checksum != record_checksum(record) ||
        !std::all_of(
            record.reserved.begin(),
            record.reserved.end(),
            [](std::uint8_t value) { return value == 0U; })) {
        return false;
    }

    const RuntimeTelemetrySnapshot snapshot{
        record.last_exit_code,
        static_cast<RuntimeRestartReason>(record.restart_reason),
        record.consecutive_failures,
        record.flags,
        record.event_filetime_utc,
    };
    return is_valid_runtime_telemetry(snapshot);
}

RuntimeTelemetrySnapshot snapshot_from_record(
    const RuntimeHistoryRecord &record) noexcept
{
    return RuntimeTelemetrySnapshot{
        record.last_exit_code,
        static_cast<RuntimeRestartReason>(record.restart_reason),
        record.consecutive_failures,
        record.flags,
        record.event_filetime_utc,
    };
}

} // namespace

RuntimeHistoryStore::RuntimeHistoryStore() noexcept
    : file_path_(default_runtime_history_file())
{
}

RuntimeHistoryStore::RuntimeHistoryStore(
    const std::filesystem::path &directory) noexcept
{
    try {
        if (!directory.empty()) {
            file_path_ = directory / kRuntimeHistoryFileName;
        }
    } catch (...) {
        file_path_.clear();
    }
}

bool RuntimeHistoryStore::load(
    RuntimeTelemetrySnapshot &snapshot) const noexcept
{
    snapshot = {};
    try {
        if (file_path_.empty()) {
            return false;
        }

        UniqueHandle file(CreateFileW(
            file_path_.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        if (!file) {
            return false;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size) ||
            size.QuadPart !=
                static_cast<LONGLONG>(sizeof(RuntimeHistoryRecord))) {
            return false;
        }

        RuntimeHistoryRecord record;
        DWORD bytes_read = 0U;
        if (!ReadFile(
                file.get(),
                &record,
                static_cast<DWORD>(sizeof(record)),
                &bytes_read,
                nullptr) ||
            bytes_read != static_cast<DWORD>(sizeof(record)) ||
            !is_valid_record(record)) {
            return false;
        }

        snapshot = snapshot_from_record(record);
        return true;
    } catch (...) {
        snapshot = {};
        return false;
    }
}

bool RuntimeHistoryStore::save(
    const RuntimeTelemetrySnapshot &snapshot) const noexcept
{
    try {
        if (file_path_.empty() ||
            !is_valid_runtime_telemetry(snapshot)) {
            return false;
        }

        std::error_code error;
        std::filesystem::create_directories(
            file_path_.parent_path(), error);
        if (error) {
            return false;
        }

        const RuntimeHistoryRecord record = make_record(snapshot);
        const std::wstring base_name =
            file_path_.wstring() + L".tmp." +
            std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(GetTickCount64());

        for (unsigned int attempt = 0U; attempt < 16U; ++attempt) {
            const std::filesystem::path temporary =
                base_name + L"." + std::to_wstring(attempt);
            UniqueHandle file(CreateFileW(
                temporary.c_str(),
                GENERIC_WRITE,
                0U,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY,
                nullptr));
            if (!file) {
                if (GetLastError() == ERROR_FILE_EXISTS ||
                    GetLastError() == ERROR_ALREADY_EXISTS) {
                    continue;
                }
                return false;
            }

            DWORD bytes_written = 0U;
            const bool written =
                WriteFile(
                    file.get(),
                    &record,
                    static_cast<DWORD>(sizeof(record)),
                    &bytes_written,
                    nullptr) != FALSE &&
                bytes_written == static_cast<DWORD>(sizeof(record)) &&
                FlushFileBuffers(file.get()) != FALSE;
            file.reset();

            if (!written) {
                DeleteFileW(temporary.c_str());
                return false;
            }

            if (MoveFileExW(
                    temporary.c_str(),
                    file_path_.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return true;
            }

            DeleteFileW(temporary.c_str());
            return false;
        }
    } catch (...) {
        return false;
    }
    return false;
}

const std::filesystem::path &RuntimeHistoryStore::file_path() const noexcept
{
    return file_path_;
}

} // namespace chatview
