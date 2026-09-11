// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/page-health-message.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

bool expect_valid(
    std::wstring_view message,
    chatview::HudProvider provider,
    chatview::HudPageState state,
    std::uint16_t detail)
{
    chatview::HudHealthSnapshot parsed;
    if (!chatview::parse_page_health_message(message, parsed)) {
        return false;
    }
    return parsed.provider == provider && parsed.state == state &&
           parsed.detail_code == detail;
}

bool expect_invalid(std::wstring_view message)
{
    chatview::HudHealthSnapshot unchanged{
        chatview::HudPageState::Ready,
        chatview::HudProvider::YouTube,
        99U};
    if (chatview::parse_page_health_message(message, unchanged)) {
        return false;
    }
    return unchanged.state == chatview::HudPageState::Ready &&
           unchanged.provider == chatview::HudProvider::YouTube &&
           unchanged.detail_code == 99U;
}

} // namespace

int main()
{
    using chatview::HudPageState;
    using chatview::HudProvider;

    if (!expect_valid(
            L"CVH1|4|4|3",
            HudProvider::YouTube,
            HudPageState::Ready,
            3U) ||
        !expect_valid(
            L"CVH1|2|7|1",
            HudProvider::Chzzk,
            HudPageState::LoginRequired,
            1U) ||
        !expect_valid(
            L"CVH1|3|8|65535",
            HudProvider::Soop,
            HudPageState::Offline,
            65535U) ||
        !expect_valid(
            L"CVH1|1|9|0",
            HudProvider::Weflab,
            HudPageState::LayoutChanged,
            0U) ||
        !expect_valid(
            L"CVH1|1|3|0",
            HudProvider::Weflab,
            HudPageState::Loading,
            0U)) {
        return fail("A valid page-health message was rejected or changed");
    }

    constexpr std::wstring_view invalid_messages[] = {
        L"",
        L"CVH0|4|4|0",
        L"CVH1|0|4|0",
        L"CVH1|5|4|0",
        L"CVH1|9|4|0",
        L"CVH1|4294967295|4|0",
        L"CVH1|4|0|0",
        L"CVH1|4|1|0",
        L"CVH1|4|5|0",
        L"CVH1|4|10|0",
        L"CVH1|4|99|0",
        L"CVH1|4|4294967295|0",
        L"CVH1|4|4|65536",
        L"CVH1|4|4|4294967295",
        L"CVH1|4|4|-1",
        L"CVH1|4|4| 1",
        L"CVH1|4|4|1 ",
        L"CVH1|4|4|01x",
        L"CVH1|4|4|1|extra",
        L"CVH1|4|4",
        L"CVH1||4|0",
        L"CVH1|4||0",
        L"CVH1|4|4|",
    };
    for (const std::wstring_view message : invalid_messages) {
        if (!expect_invalid(message)) {
            return fail("An invalid page-health message was accepted");
        }
    }

    const std::wstring overlong(65U, L'1');
    if (!expect_invalid(overlong)) {
        return fail("An overlong page-health message was accepted");
    }

    if (chatview::provider_for_chat_document(
            L"https://weflab.com/page/example") != HudProvider::Weflab ||
        chatview::provider_for_chat_document(
            L"https://chzzk.naver.com/chat/abc") != HudProvider::Chzzk ||
        chatview::provider_for_chat_document(
            L"https://m.chzzk.naver.com/chat/abc") != HudProvider::Chzzk ||
        chatview::provider_for_chat_document(
            L"https://play.sooplive.com/example") != HudProvider::Soop ||
        chatview::provider_for_chat_document(
            L"https://www.youtube.com/live_chat?v=abc") != HudProvider::YouTube ||
        chatview::provider_for_chat_document(
            L"https://example.com/chat") != HudProvider::Unknown) {
        return fail("Chat document provider detection returned an unexpected result");
    }

    if (!chatview::is_dom_reportable_page_state(HudPageState::Ready) ||
        !chatview::is_dom_reportable_page_state(HudPageState::Loading) ||
        chatview::is_dom_reportable_page_state(HudPageState::Fatal) ||
        chatview::is_dom_reportable_page_state(HudPageState::Recovering)) {
        return fail("DOM-reportable page-state policy is inconsistent");
    }

    const std::wstring_view script =
        chatview::page_health_bootstrap_script();
    if (script.find(L"CVH1|") == std::wstring_view::npos ||
        script.find(L"MutationObserver") == std::wstring_view::npos ||
        script.find(L"weflab.com") == std::wstring_view::npos ||
        script.find(L"chzzk.naver.com") == std::wstring_view::npos ||
        script.find(L"play.sooplive.com") == std::wstring_view::npos ||
        script.find(L"youtube.com") == std::wstring_view::npos) {
        return fail("The page-health bootstrap script is incomplete");
    }

    return 0;
}
