// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Windows.h>
#include <exception>
#include <thread>

namespace chatview {
// DXGI may synchronously SendMessage to the HWND owner during creation,
// Present or teardown. A plain UI-thread join can deadlock. Service only sent
// messages while the worker exits: do not dispatch posted commands, timers,
// input or consume WM_QUIT inside an object's final shutdown.
// The caller must fence reentrant start/close before entering this wait.
inline void join_capture_worker(std::thread &worker) noexcept
{
    if (!worker.joinable()) return;
    const HANDLE handle = worker.native_handle();
    for (;;) {
        const DWORD result = MsgWaitForMultipleObjectsEx(
            1, &handle, INFINITE, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
        if (result == WAIT_OBJECT_0) break;
        if (result != WAIT_OBJECT_0 + 1) std::terminate(); // invalid owned handle
        MSG message{};
        (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    worker.join();
}
}
