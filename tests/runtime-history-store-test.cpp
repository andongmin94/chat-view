// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/runtime-history-store.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdint>
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

bool overwrite_byte(
    const std::filesystem::path &path,
    std::streamoff offset,
    char value)
{
    std::fstream stream(
        path,
        std::ios::binary | std::ios::in | std::ios::out);
    if (!stream) {
        return false;
    }
    stream.seekp(offset, std::ios::beg);
    stream.put(value);
    stream.flush();
    return stream.good();
}

bool truncate_file(const std::filesystem::path &path)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    const std::uint32_t value = 0x12345678U;
    stream.write(
        reinterpret_cast<const char *>(&value),
        static_cast<std::streamsize>(sizeof(value)));
    return stream.good();
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (L"chatview-runtime-history-test-" +
         std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(
             std::chrono::steady_clock::now().time_since_epoch().count()));

    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    if (error) {
        return fail("Failed to create the runtime-history test directory");
    }

    chatview::RuntimeHistoryStore store(root);
    chatview::RuntimeTelemetrySnapshot loaded;
    if (store.load(loaded)) {
        std::filesystem::remove_all(root, error);
        return fail("A missing runtime-history file was accepted");
    }

    const chatview::RuntimeTelemetrySnapshot first{
        20U,
        chatview::RuntimeRestartReason::ConnectionRecovery,
        3U,
        chatview::RuntimeTelemetryHistoryValid |
            chatview::RuntimeTelemetryAutomatic,
        133485408000000000ULL,
    };
    if (!store.save(first) || !store.load(loaded) || loaded != first) {
        std::filesystem::remove_all(root, error);
        return fail("Runtime history did not round trip");
    }

    const chatview::RuntimeTelemetrySnapshot second{
        11U,
        chatview::RuntimeRestartReason::CaptureExclusionFailure,
        chatview::kMaximumRuntimeFailureCount,
        chatview::RuntimeTelemetryHistoryValid |
            chatview::RuntimeTelemetryAutomatic |
            chatview::RuntimeTelemetryCircuitOpen,
        133485408000000001ULL,
    };
    if (!store.save(second) || !store.load(loaded) || loaded != second) {
        std::filesystem::remove_all(root, error);
        return fail("Runtime history replacement was not atomic");
    }

    for (std::filesystem::directory_iterator iterator(root, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (iterator->path() != store.file_path()) {
            std::filesystem::remove_all(root, error);
            return fail("A runtime-history staging file was left behind");
        }
    }
    if (error) {
        std::filesystem::remove_all(root, error);
        return fail("Failed to inspect the runtime-history directory");
    }

    if (!overwrite_byte(store.file_path(), 24, '\x7f') ||
        store.load(loaded)) {
        std::filesystem::remove_all(root, error);
        return fail("A corrupted runtime-history checksum was accepted");
    }

    if (!store.save(first) || !truncate_file(store.file_path()) ||
        store.load(loaded)) {
        std::filesystem::remove_all(root, error);
        return fail("A truncated runtime-history file was accepted");
    }

    chatview::RuntimeTelemetrySnapshot invalid = first;
    invalid.flags |= 1U << 31U;
    if (store.save(invalid)) {
        std::filesystem::remove_all(root, error);
        return fail("Invalid runtime telemetry was persisted");
    }

    std::filesystem::remove_all(root, error);
    if (error) {
        return fail("Failed to remove the runtime-history test directory");
    }
    return 0;
}
