// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/display-client.hpp"
#include "hud/saved-connection.hpp"
#include <Windows.h>
#include <winrt/Windows.Data.Json.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void expect(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void await(const std::function<bool()> &done, const char *message, ULONGLONG duration = 20000)
{
    const auto deadline = GetTickCount64() + duration;
    while (!done()) { expect(GetTickCount64() < deadline, message); Sleep(10); }
}
bool received(chatview::DisplayClient &client, unsigned int minimum, bool *reconnecting = nullptr)
{
    chatview::DisplayUpdate update;
    if (!client.take(update)) return false;
    if (update.status == chatview::DisplayStatus::Reconnecting) {
        expect(update.envelope.empty(), "reconnection clears the old mailbox");
        if (reconnecting) *reconnecting = true;
    }
    expect(update.status != chatview::DisplayStatus::Failed && update.status != chatview::DisplayStatus::Denied,
           "remembered session remains authorized");
    return update.status == chatview::DisplayStatus::Receiving &&
        winrt::Windows::Data::Json::JsonObject::Parse(update.envelope).GetNamedObject(L"snapshot").GetNamedNumber(L"received") >= minimum;
}
}
int main()
{
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        std::string origin_input, ticket_input;
        std::getline(std::cin, origin_input); std::getline(std::cin, ticket_input);
        expect(origin_input.size() < 2048 && ticket_input.size() == 64, "bounded fixture input");
        const std::wstring origin(origin_input.begin(), origin_input.end()), ticket(ticket_input.begin(), ticket_input.end());
        wchar_t temporary[MAX_PATH]{}; expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary profile");
        const auto profile = std::wstring(temporary) + L"ChatView-Session-" + std::to_wstring(GetCurrentProcessId());
        expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE, "isolated profile");
        expect(SetEnvironmentVariableW(L"LOCALAPPDATA", profile.c_str()) != FALSE, "isolated credential storage");
        expect(!chatview::load_connection(), "no credential before explicit login");
        {
            chatview::DisplayClient initial;
            expect(initial.start(origin, ticket, true, chatview::DisplayAuthentication::Remember), "connect and remember login");
            await([&] { return received(initial, 2); }, "native client automatically renews its lease");
            initial.stop(); await([&] { return !initial.running(); }, "stop remembered display", 2000);
        }
        auto saved = chatview::load_connection();
        expect(saved && saved->origin == origin && saved->developer_loopback && saved->credential != ticket,
               "DPAPI restores session credential, not the one-use approval");
        chatview::DisplayClient restored;
        expect(restored.start(saved->origin, saved->credential, saved->developer_loopback,
                             chatview::DisplayAuthentication::Saved), "resume from protected login");
        await([&] { return received(restored, 3); }, "restored session receives chat");
        std::cout << "restored\n" << std::flush;
        std::string dropped;
        std::getline(std::cin, dropped);
        expect(dropped == "dropped", "fixture confirms interrupted connection");
        bool reconnecting = false;
        await([&] { return received(restored, 4, &reconnecting); }, "recover after connection and service loss");
        expect(reconnecting, "network loss surfaced as reconnecting without old chat");
        std::cout << "revoke\n" << std::flush;
        bool denied = false;
        await([&] {
            chatview::DisplayUpdate update;
            if (restored.take(update)) {
                if (update.status == chatview::DisplayStatus::Denied) denied = true;
                if (denied) expect(update.envelope.empty(), "revoked session retains no chat");
            }
            return !restored.running();
        }, "revocation stops retrying");
        chatview::DisplayUpdate last;
        if (restored.take(last) && last.status == chatview::DisplayStatus::Denied) denied = true;
        expect(denied && !chatview::load_connection(), "revocation removes stored login");
        chatview::DisplayClient stale;
        expect(stale.start(origin, saved->credential, true, chatview::DisplayAuthentication::Saved), "attempt stale credential");
        await([&] { return !stale.running(); }, "stale credential rejected");
        expect(stale.take(last) && last.status == chatview::DisplayStatus::Denied, "stale credential stays denied");
        SecureZeroMemory(saved->credential.data(), saved->credential.size() * sizeof(wchar_t));
        std::cout << "Remembered connection, protected restart, renewal, reconnect and revoke passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << "Remembered connection test failed: " << error.what() << '\n'; return 1; }
      catch (...) { std::cerr << "Remembered connection test failed\n"; return 1; }
}
