// SPDX-License-Identifier: GPL-2.0-or-later

#include "chat/twitch-irc-parser.hpp"

#include "common/utf8.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace chatview {
namespace {

constexpr std::size_t kMaximumBufferedBytes = 64U * 1024U;
constexpr std::size_t kMaximumAuthorBytes = 96U;
constexpr std::size_t kMaximumMessageBytes = 2048U;

std::string unescape_tag_value(std::string_view value)
{
    std::string result;
    result.reserve(value.size());

    bool escaped = false;
    for (const char character : value) {
        if (!escaped) {
            if (character == '\\') {
                escaped = true;
            } else {
                result.push_back(character);
            }
            continue;
        }

        switch (character) {
        case 's':
            result.push_back(' ');
            break;
        case ':':
            result.push_back(';');
            break;
        case 'r':
            result.push_back('\r');
            break;
        case 'n':
            result.push_back('\n');
            break;
        case '\\':
            result.push_back('\\');
            break;
        default:
            result.push_back(character);
            break;
        }
        escaped = false;
    }

    if (escaped) {
        result.push_back('\\');
    }

    return result;
}

std::optional<std::string> find_tag(std::string_view tags, std::string_view key)
{
    std::size_t start = 0U;
    while (start <= tags.size()) {
        const std::size_t end = tags.find(';', start);
        const std::string_view entry = tags.substr(
            start,
            end == std::string_view::npos ? tags.size() - start : end - start);
        const std::size_t separator = entry.find('=');
        const std::string_view entry_key = entry.substr(0U, separator);
        if (entry_key == key) {
            if (separator == std::string_view::npos) {
                return std::string();
            }
            return unescape_tag_value(entry.substr(separator + 1U));
        }

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }

    return std::nullopt;
}

std::string prefix_nickname(std::string_view prefix)
{
    const std::size_t separator = prefix.find('!');
    return std::string(prefix.substr(0U, separator));
}

void trim_ascii(std::string &value)
{
    const auto not_space = [](unsigned char character) {
        return std::isspace(character) == 0;
    };

    const auto first = std::find_if(value.begin(), value.end(), not_space);
    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    if (first >= last) {
        value.clear();
        return;
    }

    value.assign(first, last);
}

} // namespace

std::vector<std::string> IrcLineBuffer::append(std::string_view bytes)
{
    if (bytes.empty()) {
        return {};
    }

    if (pending_.size() + bytes.size() > kMaximumBufferedBytes) {
        pending_.clear();
        return {};
    }

    pending_.append(bytes);

    std::vector<std::string> lines;
    std::size_t start = 0U;
    while (true) {
        const std::size_t end = pending_.find("\r\n", start);
        if (end == std::string::npos) {
            break;
        }

        lines.emplace_back(pending_.substr(start, end - start));
        start = end + 2U;
    }

    if (start > 0U) {
        pending_.erase(0U, start);
    }

    return lines;
}

void IrcLineBuffer::clear() noexcept
{
    pending_.clear();
}

std::optional<ParsedChatMessage> parse_twitch_privmsg(std::string_view line)
{
    std::string_view tags;
    if (!line.empty() && line.front() == '@') {
        const std::size_t tag_end = line.find(' ');
        if (tag_end == std::string_view::npos) {
            return std::nullopt;
        }
        tags = line.substr(1U, tag_end - 1U);
        line.remove_prefix(tag_end + 1U);
    }

    std::string_view prefix;
    if (!line.empty() && line.front() == ':') {
        const std::size_t prefix_end = line.find(' ');
        if (prefix_end == std::string_view::npos) {
            return std::nullopt;
        }
        prefix = line.substr(1U, prefix_end - 1U);
        line.remove_prefix(prefix_end + 1U);
    }

    const std::size_t command_end = line.find(' ');
    const std::string_view command = line.substr(0U, command_end);
    if (command != "PRIVMSG" || command_end == std::string_view::npos) {
        return std::nullopt;
    }

    line.remove_prefix(command_end + 1U);
    const std::size_t trailing = line.find(" :");
    if (trailing == std::string_view::npos) {
        return std::nullopt;
    }

    std::string text(line.substr(trailing + 2U));
    if (text.size() >= 9U && text.front() == '\x01' && text.back() == '\x01' &&
        text.compare(1U, 7U, "ACTION ") == 0) {
        text = text.substr(8U, text.size() - 9U);
    }
    trim_ascii(text);
    if (text.empty()) {
        return std::nullopt;
    }

    std::string author;
    if (!tags.empty()) {
        const std::optional<std::string> display_name = find_tag(tags, "display-name");
        if (display_name.has_value()) {
            author = *display_name;
        }
    }
    if (author.empty()) {
        author = prefix_nickname(prefix);
    }
    trim_ascii(author);
    if (author.empty()) {
        author = "Viewer";
    }

    ParsedChatMessage message;
    message.author = truncate_utf8(author, kMaximumAuthorBytes);
    message.text = truncate_utf8(text, kMaximumMessageBytes);
    return message;
}

std::optional<std::string> twitch_pong_for_ping(std::string_view line)
{
    if (line == "PING") {
        return std::string("PONG\r\n");
    }
    if (line.size() > 5U && line.substr(0U, 5U) == "PING ") {
        return std::string("PONG ") + std::string(line.substr(5U)) + "\r\n";
    }
    return std::nullopt;
}

} // namespace chatview
