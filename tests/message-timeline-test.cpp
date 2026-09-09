// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/message-timeline.hpp"

#include <chrono>
#include <iostream>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    using namespace std::chrono_literals;
    using Clock = chatview::MessageTimeline::Clock;

    const Clock::time_point start{};
    chatview::MessageTimeline timeline(2U, 10s);

    timeline.push(L"ignored", L"", start);
    if (timeline.size() != 0U) {
        return fail("Empty chat text was accepted");
    }

    timeline.push(L"", L"first", start);
    timeline.push(L"second", L"second message", start + 1s);
    timeline.push(L"third", L"third message", start + 2s);

    const auto bounded = timeline.snapshot();
    if (bounded.size() != 2U || bounded[0].author != L"second" ||
        bounded[1].author != L"third") {
        return fail("Message capacity was not enforced in arrival order");
    }

    if (!timeline.expire(start + 12s)) {
        return fail("Expired messages did not report a change");
    }
    const auto remaining = timeline.snapshot();
    if (remaining.size() != 1U || remaining[0].author != L"third") {
        return fail("Message expiry removed the wrong entries");
    }

    if (!timeline.expire(start + 13s) || timeline.size() != 0U) {
        return fail("The final expired message was retained");
    }
    if (timeline.expire(start + 20s)) {
        return fail("An empty expiry pass reported a change");
    }

    timeline.push(L"", L"default author", start + 21s);
    const auto default_author = timeline.snapshot();
    if (default_author.size() != 1U || default_author[0].author != L"Viewer") {
        return fail("The default chat author was not applied");
    }

    timeline.clear();
    if (timeline.size() != 0U) {
        return fail("Timeline clear failed");
    }

    return 0;
}
