// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/message-timeline.hpp"

#include <algorithm>
#include <utility>

namespace chatview {

MessageTimeline::MessageTimeline(std::size_t capacity, std::chrono::seconds lifetime)
    : capacity_(std::max<std::size_t>(1U, capacity)),
      lifetime_(std::max(std::chrono::seconds(1), lifetime))
{
}

void MessageTimeline::push(std::wstring author,
                           std::wstring text,
                           Clock::time_point received_at)
{
    if (text.empty()) {
        return;
    }

    expire(received_at);

    if (author.empty()) {
        author = L"Viewer";
    }

    messages_.push_back(ChatMessage{
        .author = std::move(author),
        .text = std::move(text),
        .received_at = received_at,
    });

    while (messages_.size() > capacity_) {
        messages_.pop_front();
    }
}

void MessageTimeline::expire(Clock::time_point now) noexcept
{
    while (!messages_.empty() && now - messages_.front().received_at >= lifetime_) {
        messages_.pop_front();
    }
}

void MessageTimeline::clear() noexcept
{
    messages_.clear();
}

std::vector<ChatMessage> MessageTimeline::snapshot() const
{
    return {messages_.begin(), messages_.end()};
}

std::size_t MessageTimeline::size() const noexcept
{
    return messages_.size();
}

} // namespace chatview
