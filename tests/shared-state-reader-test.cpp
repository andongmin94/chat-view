// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/shared-state.hpp"
#include "common/win32-handle.hpp"
#include "hud/shared-state-reader.hpp"

#include <Windows.h>

#include <iostream>
#include <string>

namespace {

class MappedState final {
public:
    explicit MappedState(chatview::SharedState *state) noexcept
        : state_(state)
    {
    }

    ~MappedState()
    {
        if (state_ != nullptr) {
            UnmapViewOfFile(state_);
        }
    }

    MappedState(const MappedState &) = delete;
    MappedState &operator=(const MappedState &) = delete;

    [[nodiscard]] chatview::SharedState *get() const noexcept
    {
        return state_;
    }

private:
    chatview::SharedState *state_ = nullptr;
};

int fail(const wchar_t *message)
{
    std::wcerr << message << L'\n';
    return 1;
}

void publish(
    chatview::SharedState *state,
    std::uint32_t flags,
    std::uint64_t generation) noexcept
{
    InterlockedIncrement(&state->sequence);
    MemoryBarrier();
    state->flags = flags;
    state->generation = generation;
    MemoryBarrier();
    InterlockedIncrement(&state->sequence);
}

} // namespace

int main()
{
    const std::wstring suffix =
        std::to_wstring(GetCurrentProcessId());
    const std::wstring mapping_name =
        L"Local\\ChatViewOBS.ReaderTest.State." + suffix;
    const std::wstring event_name =
        L"Local\\ChatViewOBS.ReaderTest.Event." + suffix;

    chatview::UniqueHandle mapping(CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0U,
        static_cast<DWORD>(sizeof(chatview::SharedState)),
        mapping_name.c_str()));
    if (!mapping) {
        return fail(L"Failed to create the reader-test mapping");
    }

    MappedState mapped(static_cast<chatview::SharedState *>(MapViewOfFile(
        mapping.get(),
        FILE_MAP_ALL_ACCESS,
        0U,
        0U,
        sizeof(chatview::SharedState))));
    if (mapped.get() == nullptr) {
        return fail(L"Failed to map the reader-test state");
    }

    chatview::UniqueHandle event(
        CreateEventW(nullptr, FALSE, FALSE, event_name.c_str()));
    if (!event) {
        return fail(L"Failed to create the reader-test event");
    }

    ZeroMemory(mapped.get(), sizeof(chatview::SharedState));
    mapped.get()->magic = chatview::kSharedStateMagic;
    mapped.get()->version = chatview::kSharedStateVersion;

    chatview::SharedStateReader reader;
    if (!reader.open(
            mapping_name,
            event_name,
            GetCurrentProcessId())) {
        return fail(L"SharedStateReader did not open a valid transport");
    }
    if (reader.state_changed_event() == nullptr ||
        reader.parent_process() == nullptr) {
        return fail(L"SharedStateReader did not expose valid wait handles");
    }
    if (WaitForSingleObject(reader.parent_process(), 0U) != WAIT_TIMEOUT) {
        return fail(L"The current-process parent handle was unexpectedly signaled");
    }

    publish(
        mapped.get(),
        chatview::SharedStateStreaming |
            chatview::SharedStateRecording,
        42U);

    chatview::SharedSnapshot snapshot;
    if (!reader.read(snapshot) ||
        snapshot.flags !=
            (chatview::SharedStateStreaming |
             chatview::SharedStateRecording) ||
        snapshot.generation != 42U) {
        return fail(L"SharedStateReader changed a valid snapshot");
    }

    if (!SetEvent(event.get()) ||
        WaitForSingleObject(
            reader.state_changed_event(), 1000U) != WAIT_OBJECT_0) {
        return fail(L"SharedStateReader event handle did not receive a wakeup");
    }

    const chatview::SharedSnapshot unchanged = snapshot;
    publish(mapped.get(), 1U << 2U, 43U);
    if (reader.read(snapshot) ||
        snapshot.flags != unchanged.flags ||
        snapshot.generation != unchanged.generation) {
        return fail(L"SharedStateReader accepted an unknown state flag");
    }

    publish(mapped.get(), chatview::SharedStateStreaming, 44U);
    mapped.get()->magic = 0U;
    if (reader.read(snapshot)) {
        return fail(L"SharedStateReader accepted an invalid magic value");
    }
    mapped.get()->magic = chatview::kSharedStateMagic;

    mapped.get()->version = chatview::kSharedStateVersion + 1U;
    if (reader.read(snapshot)) {
        return fail(L"SharedStateReader accepted an unknown protocol version");
    }
    mapped.get()->version = chatview::kSharedStateVersion;

    InterlockedExchange(&mapped.get()->sequence, 1L);
    if (reader.read(snapshot)) {
        return fail(L"SharedStateReader accepted an in-progress write");
    }
    InterlockedExchange(&mapped.get()->sequence, 2L);

    publish(
        mapped.get(),
        chatview::SharedStateShutdown,
        45U);
    if (!reader.read(snapshot) ||
        snapshot.flags != chatview::SharedStateShutdown ||
        snapshot.generation != 45U) {
        return fail(L"SharedStateReader did not recover after malformed states");
    }

    reader.close();
    if (reader.state_changed_event() != nullptr ||
        reader.parent_process() != nullptr ||
        reader.read(snapshot)) {
        return fail(L"SharedStateReader retained state after close");
    }

    chatview::SharedStateReader missing;
    if (missing.open(
            mapping_name + L".missing",
            event_name + L".missing",
            GetCurrentProcessId())) {
        return fail(L"SharedStateReader opened a nonexistent transport");
    }

    return 0;
}
