// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/launch-options.hpp"
#include "hud/connection-status.hpp"

#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::size_t assertions = 0U;
void expect(bool condition, const char *message)
{
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
auto parse(std::initializer_list<std::wstring_view> arguments)
{
    return chatview::parse_hud_launch_options({arguments.begin(), arguments.size()});
}
}
int main()
{
    try {
        const auto companion = parse({L"--companion"});
        expect(companion.has_value(), "explicit companion launches without OBS arguments");
        expect(companion->mode == chatview::HudLaunchMode::Companion, "companion role");
        expect(companion->parent_process_id == 0U && companion->mapping_name.empty() &&
               companion->event_name.empty() && companion->ready_event_name.empty(),
               "no fake parent, controller mapping or readiness name");
        expect(!parse({}), "empty command line does not fall back to companion");
        expect(!parse({L"--companion", L"--companion"}), "duplicate companion denied");
        for (const auto option : {L"--parent", L"--mapping", L"--event", L"--ready-event"}) {
            expect(!parse({L"--companion", option, L"123"}), "mixed role arguments denied");
            expect(!parse({option, L"123", L"--companion"}), "mixed role argument order denied");
            expect(!parse({L"--companion", option}), "companion with missing parameter denied");
        }
        const auto obs = parse({L"--mapping", L"Local\\map", L"--event", L"Local\\event",
                                L"--parent", L"123", L"--ready-event", L"Local\\ready"});
        expect(obs && obs->mode == chatview::HudLaunchMode::ObsControlled, "OBS role retained");
        expect(obs->mapping_name == L"Local\\map" && obs->event_name == L"Local\\event" &&
               obs->ready_event_name == L"Local\\ready" && obs->parent_process_id == 123U,
               "OBS arguments retained");
        expect(parse({L"--parent", L"4294967295", L"--event", L"e", L"--mapping", L"m"}).has_value(),
               "DWORD maximum accepted");
        for (const auto bad : {L"0", L"-1", L"+1", L" 1", L"1 ", L"1x", L"0x42",
                               L"4294967296", L"99999999999999999999", L""}) {
            expect(!parse({L"--mapping", L"m", L"--event", L"e", L"--parent", bad}),
                   "invalid PID rejected without fallback");
        }
        for (const auto name : {L"--mapping", L"--event", L"--ready-event", L"--parent"}) {
            const std::vector<std::wstring_view> args = {L"--mapping", L"m", L"--event", L"e",
                L"--parent", L"1", L"--ready-event", L"r", name, L"2"};
            expect(!chatview::parse_hud_launch_options(args), "duplicate option rejected");
        }
        expect(!parse({L"--mapping", L"m", L"--event", L"e"}), "missing OBS parent rejected");
        expect(!parse({L"--mapping", L"", L"--event", L"e", L"--parent", L"1"}), "empty mapping rejected");
        expect(!parse({L"--mapping", L"--event", L"--parent", L"1"}), "flag cannot be a value");
        expect(!parse({L"--token", L"private"}), "credentials not accepted on command line");
        const std::wstring nul(L"m\0x", 3U);
        expect(!parse({L"--mapping", nul, L"--event", L"e", L"--parent", L"1"}), "embedded NUL rejected");
        std::wstring mapping = L"owned-copy";
        const auto owned = parse({L"--mapping", mapping, L"--event", L"e", L"--parent", L"1"});
        mapping.assign(L"changed");
        expect(owned && owned->mapping_name == L"owned-copy", "options own strings after argv release");
        expect(chatview::display_role_for(companion->mode) == chatview::DisplayRole::Gaming,
            "companion requests gaming role without an OBS dependency");
        expect(chatview::display_role_for(obs->mode) == chatview::DisplayRole::Streaming,
            "validated OBS launch requests streaming role");
        expect(std::wstring_view(chatview::display_role_wire(chatview::DisplayRole::Gaming)) == L"gaming" &&
            std::wstring_view(chatview::display_role_wire(chatview::DisplayRole::Streaming)) == L"streaming",
            "wire role spelling is fixed");
        const std::wstring id = L"01234567-89ab-cdef-0123-456789abcdef";
        expect(chatview::connection_id(id), "canonical UUID accepted");
        for (const auto &bad : {std::wstring(), id + L"x", id.substr(1),
            std::wstring(L"01234567_89ab-cdef-0123-456789abcdef"),
            std::wstring(L"01234567-89AB-cdef-0123-456789abcdef"),
            std::wstring(L"01234567-89ab-cdef-0123-456789abcdeg")})
            expect(!chatview::connection_id(bad), "malformed session identifiers rejected");
        chatview::DisplayConnectionState state{{chatview::DisplayRole::Gaming, id, id}, 1, 1};
        const auto summary = chatview::connection_summary(state);
        expect(summary.find(id) != std::wstring::npos && summary.find(L"게임 1 / 송출 1") != std::wstring::npos,
            "summary identifies the exact session and display counts");
        expect(summary.find(L"영상 제외 미검증") != std::wstring::npos,
            "role approval is explicitly not a capture-safety claim");
        const auto copy = state;
        expect(state == copy, "connection status equality retains membership");
        state.membership.role = chatview::DisplayRole::Streaming;
        expect(state != copy, "different role is not the same connection status");
        std::cout << "HUD launch options and connection status: " << assertions << " assertions passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
