// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/obs-output.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned assertions = 0;
void expect(bool value) { ++assertions; if (!value) throw std::runtime_error("output-state assertion failed"); }
}
int main()
{
    try {
        using namespace chatview;
        for (const bool stream : {false, true}) for (const bool record : {false, true}) {
            const auto value = unpack_obs_output(pack_obs_output({10000U, stream, record}));
            expect(value.sampled_tick == 10000U && value.streaming == stream && value.recording == record);
            expect(fresh_obs_output(value, 10000U));
            expect(fresh_obs_output(value, 12999U));
            expect(!fresh_obs_output(value, 13000U));
            expect(!fresh_obs_output(value, 9999U));
        }
        expect(pack_obs_output({}) == 0U);
        expect(!fresh_obs_output(unpack_obs_output(0U), 1000U));
        expect(pack_obs_output({std::numeric_limits<std::uint64_t>::max(), true, true}) == 0U);
        ReportedOutput remote{15000U, true, false};
        expect(!expire_reported_output(remote, 14999U));
        expect(remote.streaming);
        expect(expire_reported_output(remote, 15000U));
        expect(remote == ReportedOutput{});
        expect(!expire_reported_output(remote, 20000U));
        remote = {30000U, false, false};
        expect(!expire_reported_output(remote, 29999U));
        expect(remote.expires_tick != 0U); // A reported stop is distinct from unknown.
        expect(expire_reported_output(remote, 30000U));
        std::cout << "OBS output freshness: " << assertions << " assertions passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
