// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string_view>

namespace chatview {

// A LOCAL OBS Browser Source URL candidate, not a proof that it belongs to
// this creator's selected campaign, rendered a pixel, or reached viewers.
// The public test route is exact: /public/ads/ + 32 lowercase hex chars.
// HTTP is permitted only for the development loopback service.
constexpr bool is_test_banner_source_url(std::string_view url) noexcept
{
    if (url.size() > 2048) return false; // Bound untrusted OBS source settings.
    constexpr std::string_view path = "/public/ads/";
    const bool secure = url.starts_with("https://");
    const auto offset = secure ? 8U : url.starts_with("http://") ? 7U : 0U;
    if (!offset) return false;
    const auto split = url.find('/', offset);
    if (split == std::string_view::npos || split == offset) return false;
    const auto authority = url.substr(offset, split - offset);
    for (const unsigned char ch : authority)
        if (ch <= 0x20 || ch == 0x7f || ch == '@' || ch == '\\' ||
            ch == '?' || ch == '#') return false;
    if (!secure) {
        constexpr std::string_view loopback = "127.0.0.1:";
        if (!authority.starts_with(loopback) || authority.size() == loopback.size())
            return false;
        for (const char digit : authority.substr(loopback.size()))
            if (digit < '0' || digit > '9') return false;
    }
    const auto route = url.substr(split);
    if (!route.starts_with(path) || route.size() != path.size() + 32) return false;
    for (const char digit : route.substr(path.size()))
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
            return false;
    return true;
}

struct PublicBannerSceneCheck {
    static constexpr unsigned max_items = 256;
    unsigned inspected = 0;
    unsigned matching = 0;
    unsigned visible = 0;
    bool truncated = false;

    // Returns false when the read-only direct-scene scan must stop. No
    // renderer call, network request, ad authority or persistent data.
    bool observe(std::string_view source_id, std::string_view url, bool item_visible) noexcept
    {
        if (inspected == max_items) { truncated = true; return false; }
        ++inspected;
        if (source_id == "browser_source" && is_test_banner_source_url(url)) {
            ++matching;
            if (item_visible) ++visible;
        }
        return true;
    }
};

} // namespace chatview
