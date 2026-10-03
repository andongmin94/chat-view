// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/native-chat-control.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
unsigned assertions = 0;
void expect(bool condition, const char *message)
{
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
}
int main()
{
    using namespace chatview;
    try {
        constexpr std::array pages{
            HudPageState::Unknown, HudPageState::Unknown, HudPageState::LoginRequired,
            HudPageState::Loading, HudPageState::Ready, HudPageState::ConnectionLost,
            HudPageState::ConnectionLost, HudPageState::SystemPaused};
        for (std::uint64_t value = 0; value < pages.size(); ++value) {
            const auto status = decode_native_chat_status(value);
            expect(static_cast<std::uint64_t>(status) == value, "known reply decodes exactly");
            expect(has_native_chat_flow(status) == (value >= 2), "idle/unknown do not become an approved flow");
            expect(native_chat_page_state(status) == pages[static_cast<std::size_t>(value)], "local page state matches native phase");
            expect((native_chat_page_state(status) == HudPageState::Ready) == (status == NativeChatStatus::Receiving),
                "only a receiving surface can contribute local readiness");
            const std::wstring_view label(native_chat_status_text(status));
            expect(!label.empty() && label.size() < 96, "bounded status label");
            expect(label.find(L"STREAM READY") == std::wstring_view::npos && label.find(L"safe") == std::wstring_view::npos,
                "local reply does not certify audience video");
        }
        for (const std::uint64_t invalid : std::array<std::uint64_t, 5>{8, 255, 0x100000004ULL, 0x8000000000000000ULL,
                std::numeric_limits<std::uint64_t>::max()}) {
            const auto status = decode_native_chat_status(invalid);
            expect(status == NativeChatStatus::Unavailable, "reject the entire unknown reply, not just low bits");
            expect(!has_native_chat_flow(status), "unknown reply cannot bypass external configuration checks");
            expect(native_chat_page_state(status) != HudPageState::Ready, "unknown reply cannot inherit ready");
        }
        expect(std::wstring_view(kOpenNativeChatMessageName) != kQueryNativeChatMessageName,
            "open action and read-only query are distinct");
        expect(native_chat_page_state(NativeChatStatus::AwaitingApproval) == HudPageState::LoginRequired,
            "opening a login request is not approval");
        expect(std::wstring_view(native_chat_status_text(NativeChatStatus::SigningOut)).find(L"pending") != std::wstring_view::npos,
            "logout intent does not imply server acknowledgement");
        std::cout << "Native chat control: " << assertions << " assertions passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
