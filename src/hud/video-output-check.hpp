// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace chatview {
// Local visual-check lifetime only. This is neither a device enrollment code
// nor receiver/capture attestation. No input code or server authority exists.
class VideoOutputCheck final {
public:
    static constexpr std::uint64_t lifetime_ms = 120000;
    bool begin(std::uint64_t now, std::uint64_t selection, std::uint32_t identifier) noexcept
    {
        reset();
        if (!identifier || identifier > 0xffffffU) return false;
        started_ = now; selection_ = selection; identifier_ = identifier; active_ = true;
        return true;
    }
    void reset() noexcept { active_ = false; painted_ = false; identifier_ = 0; }
    bool active() const noexcept { return active_; }
    bool current(std::uint64_t now, std::uint64_t selection) const noexcept
    {
        return active_ && selection == selection_ && now >= started_ && now - started_ < lifetime_ms;
    }
    void painted(bool success) noexcept { painted_ = active_ && success; }
    bool ready(std::uint64_t now, std::uint64_t selection) const noexcept
    {
        return painted_ && current(now, selection);
    }
    bool confirm(std::uint64_t now, std::uint64_t selection) noexcept
    {
        const bool valid = ready(now, selection);
        reset(); // An attempted confirmation is never reusable.
        return valid;
    }
    std::uint32_t identifier() const noexcept { return identifier_; }
    unsigned seconds(std::uint64_t now) const noexcept
    {
        return active_ && now >= started_ && now - started_ < lifetime_ms
            ? static_cast<unsigned>((now - started_) / 1000U) : 0U;
    }
private:
    std::uint64_t started_ = 0, selection_ = 0;
    std::uint32_t identifier_ = 0;
    bool active_ = false, painted_ = false;
};
inline std::array<wchar_t, 7> video_check_label(std::uint32_t identifier) noexcept
{
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::array<wchar_t, 7> result{};
    for (unsigned i = 0; i < 6; ++i) result[i] = digits[(identifier >> ((5U - i) * 4U)) & 15U];
    return result;
}
}
