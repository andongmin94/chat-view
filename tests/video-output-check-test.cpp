// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/video-output-check.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

int main()
{
    unsigned checks = 0;
    const auto expect = [&](bool value) { ++checks; if (!value) throw std::runtime_error("output check assertion"); };
    try {
        chatview::VideoOutputCheck check;
        expect(!check.active() && !check.ready(1000, 1));
        expect(!check.confirm(1000, 1));
        expect(!check.begin(1000, 1, 0));
        expect(!check.begin(1000, 1, 0x1000000));
        expect(check.begin(1000, 7, 0x12abef));
        expect(check.active() && check.current(1000, 7));
        expect(!check.ready(1000, 7)); // Must successfully paint first.
        check.painted(true);
        expect(check.ready(1000, 7));
        expect(check.seconds(6500) == 5);
        expect(check.ready(120999, 7));
        expect(!check.ready(121000, 7));
        expect(!check.ready(999, 7));
        expect(!check.ready(1100, 8));
        expect(!check.confirm(121000, 7) && !check.active());
        expect(check.begin(2000, 8, 0x12abef)); check.painted(true);
        expect(!check.confirm(2001, 9) && !check.active());
        expect(!check.confirm(2002, 8));
        expect(check.begin(3000, 10, 1)); check.painted(true);
        expect(check.confirm(4000, 10) && !check.active() && check.identifier() == 0);
        expect(!check.confirm(4001, 10));
        expect(check.begin(5000, 11, 2)); check.painted(true); check.reset();
        expect(!check.ready(5001, 11)); // Stop, notification and selection change.
        check.painted(true); expect(!check.active() && !check.ready(5002, 11));
        expect(check.begin(6000, 12, 3)); check.painted(true); check.painted(false);
        expect(!check.confirm(6001, 12)); // Painting failure cannot authorize.
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        expect(check.begin(maximum - 1000, 12, 4)); check.painted(true);
        expect(check.ready(maximum, 12));
        expect(!check.ready(0, 12)); // No deadline addition overflow/clock revival.
        const auto label = chatview::video_check_label(0x12abef);
        expect(std::wstring_view(label.data()) == L"12ABEF");
        expect(std::wstring_view(chatview::video_check_label(1).data()) == L"000001");
        expect(std::wstring_view(chatview::video_check_label(0xffffff).data()) == L"FFFFFF");
        expect(label.back() == L'\0');
        std::cout << "Video output check: " << checks << " assertions passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
