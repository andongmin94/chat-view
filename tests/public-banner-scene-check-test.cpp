// SPDX-License-Identifier: GPL-2.0-or-later
// Production URL predicate/scene-item classification. No OBS frontend,
// provider, creator account, network, ad exposure or actual pixels are faked.
#include "plugin/public-banner-scene-check.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
unsigned assertions = 0;
void expect(bool condition, const char *reason)
{
    ++assertions;
    if (!condition) throw std::runtime_error(reason);
}
}

int main()
{
    try {
        using chatview::is_test_banner_source_url;
        using chatview::PublicBannerSceneCheck;
        const std::string token(32, 'a');
        const std::string url = "https://chatview.example/public/ads/" + token;
        const std::string local = "http://127.0.0.1:3210/public/ads/" + token;
        expect(is_test_banner_source_url(url), "exact public HTTPS source candidate");
        expect(is_test_banner_source_url(local), "only loopback local development HTTP");
        for (const std::string &invalid : {
            std::string("http://chatview.example/public/ads/") + token,
            std::string("http://localhost:3210/public/ads/") + token,
            std::string("http://127.0.0.1.evil:3210/public/ads/") + token,
            std::string("http://127.0.0.1:abc/public/ads/") + token,
            std::string("https://user:secret@chatview.example/public/ads/") + token,
            std::string("https://chatview.example\\path/public/ads/") + token,
            std::string("https://chatview.example/public/ads/") + token.substr(0, 31),
            std::string("https://chatview.example/public/ads/") + token + "/state",
            std::string("https://chatview.example/public/ads/") + token + "?token=secret",
            std::string("https://chatview.example/public/ads/") + token + "#anchor",
            std::string("https://chatview.example/public/ads/") + std::string(32, 'A'),
            std::string("https://chatview.example/public/ad/") + token,
            std::string("https://chatview.example/not-public/ads/") + token,
            std::string("file:///public/ads/") + token,
            std::string("https:///public/ads/") + token,
            std::string(2049, 'x'),
        }) {
            expect(!is_test_banner_source_url(invalid), "never mark an unrelated or credentialed URL as a candidate");
        }
        PublicBannerSceneCheck scan;
        expect(scan.observe("browser_source", url, true), "visible direct candidate inspected");
        expect(scan.observe("browser_source", url, false), "hidden direct candidate inspected");
        expect(scan.observe("browser_source", local, true), "local development candidate inspected");
        expect(scan.observe("browser_source", "https://other.example/", true), "unrelated browser source inspected");
        expect(scan.observe("game_capture", url, true), "an unrelated input type is not a browser source");
        expect(scan.matching == 3 && scan.visible == 2 && scan.inspected == 5 && !scan.truncated,
            "hidden/visible source candidates are distinct from unrelated inputs");
        for (unsigned i = scan.inspected; i < PublicBannerSceneCheck::max_items; ++i)
            expect(scan.observe("other", "", false), "bound direct OBS scene read");
        expect(!scan.observe("browser_source", url, true) && scan.truncated,
            "more than 256 direct items must produce an incomplete, not successful result");
        expect(scan.matching == 3 && scan.visible == 2,
            "overflow never turns an uninspected source into a verified candidate");

        std::cout << "OBS public test source candidate: " << assertions
                  << " predicate/bounded-scene assertions passed (no renderer or viewer evidence)\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
