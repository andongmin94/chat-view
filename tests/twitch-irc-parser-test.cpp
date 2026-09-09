// SPDX-License-Identifier: GPL-2.0-or-later

#include "chat/twitch-irc-parser.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    chatview::IrcLineBuffer buffer;
    if (!buffer.append("PING :tmi.twitch.tv\r").empty()) {
        return fail("A partial IRC line was emitted");
    }

    const auto lines = buffer.append(
        "\n@display-name=Kim\\sSanghoon;color=#fff :user!user@user.tmi.twitch.tv "
        "PRIVMSG #channel :hello world\r\n");
    if (lines.size() != 2U || lines[0] != "PING :tmi.twitch.tv") {
        return fail("IRC line buffering failed across chunks");
    }

    const auto pong = chatview::twitch_pong_for_ping(lines[0]);
    if (!pong.has_value() || *pong != "PONG :tmi.twitch.tv\r\n") {
        return fail("Twitch PING response was not generated");
    }

    const auto message = chatview::parse_twitch_privmsg(lines[1]);
    if (!message.has_value() || message->author != "Kim Sanghoon" ||
        message->text != "hello world") {
        return fail("Tagged Twitch PRIVMSG parsing failed");
    }

    const auto fallback = chatview::parse_twitch_privmsg(
        ":plain_user!plain_user@host PRIVMSG #channel :fallback author");
    if (!fallback.has_value() || fallback->author != "plain_user") {
        return fail("Prefix nickname fallback failed");
    }

    const std::string action_line =
        "@display-name=Actor :actor!actor@host PRIVMSG #channel :\x01ACTION waves\x01";
    const auto action = chatview::parse_twitch_privmsg(action_line);
    if (!action.has_value() || action->text != "waves") {
        return fail("IRC ACTION normalization failed");
    }

    if (chatview::parse_twitch_privmsg("NOTICE * :not chat").has_value()) {
        return fail("A non-PRIVMSG line was accepted as chat");
    }
    if (chatview::twitch_pong_for_ping("PONG :tmi.twitch.tv").has_value()) {
        return fail("A non-PING line generated a PONG");
    }

    buffer.clear();
    const std::string oversized(70U * 1024U, 'x');
    if (!buffer.append(oversized).empty()) {
        return fail("Oversized IRC input emitted a line");
    }
    if (!buffer.append("PING\r\n").empty()) {
        return fail("The overflow reset retained stale bytes");
    }

    return 0;
}
