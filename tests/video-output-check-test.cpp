// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/video-output-check.hpp"
#include "hud/video-frame-time.hpp"
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

        using chatview::video_frame_fresh;
        using chatview::video_frame_time;
        expect(!video_frame_fresh(1000, 0));
        expect(video_frame_fresh(1000, 1000));
        expect(video_frame_fresh(2999, 1000));
        expect(!video_frame_fresh(3000, 1000));
        expect(!video_frame_fresh(1000, 1001));
        expect(video_frame_fresh(maximum, maximum - 1999));
        expect(!video_frame_fresh(maximum, maximum - 2000));
        expect(!video_frame_fresh(0, maximum));
        expect(video_frame_time(10000, 0) == 10000);
        expect(video_frame_time(10000, -0.1) == 10000);
        expect(video_frame_time(10000, -0.1001) == 0);
        expect(video_frame_time(10000, 1.5) == 8500);
        expect(video_frame_time(10000, 0.0001) == 9999);
        expect(!video_frame_fresh(10000, video_frame_time(10000, 1.9999)));
        expect(video_frame_time(10000, 2.0) == 0);
        expect(video_frame_time(10000, 3.0) == 0);
        expect(video_frame_time(0, 0) == 0);
        expect(video_frame_time(500, 0.5) == 0); // No unsigned underflow.
        expect(video_frame_time(1, 0) == 1);
        expect(video_frame_time(10000, std::numeric_limits<double>::quiet_NaN()) == 0);
        expect(video_frame_time(10000, std::numeric_limits<double>::infinity()) == 0);
        expect(video_frame_time(10000, -std::numeric_limits<double>::infinity()) == 0);
        const auto captured = video_frame_time(10000, 0.5);
        expect(video_frame_fresh(11499, captured));
        expect(!video_frame_fresh(11500, captured)); // A slow present cannot renew content age.
        expect(!video_frame_fresh(14000, captured));
        std::cout << "Video output check and frame freshness: " << checks << " assertions passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
