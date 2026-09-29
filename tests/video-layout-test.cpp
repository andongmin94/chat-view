// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/video-layout.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>

int main()
{
    unsigned assertions = 0;
    const auto expect = [&](bool value) { ++assertions; if (!value) throw std::runtime_error("video layout assertion"); };
    try {
        using namespace chatview;
        expect(video_fit(1920, 1080, 1920, 1080) == VideoRect{0, 0, 1920, 1080});
        expect(video_fit(1920, 1080, 1280, 1024) == VideoRect{0, 152, 1280, 872});
        expect(video_fit(1024, 768, 1920, 1080) == VideoRect{240, 0, 1680, 1080});
        expect(video_fit(4096, 1, 1, 4096) == VideoRect{0, 2047, 1, 2048});
        for (const int size : {-1, 0, 4097, std::numeric_limits<int>::max()}) {
            expect(!video_size(size, 100)); expect(!video_size(100, size));
            expect(video_fit(size, 100, 100, 100) == VideoRect{});
            expect(video_fit(100, 100, size, 100) == VideoRect{});
        }
        for (int sw : {1, 320, 1920, 4096}) for (int sh : {1, 240, 1080, 4096}) {
            const auto r = video_fit(sw, sh, 1919, 1079);
            expect(r.right > r.left && r.bottom > r.top && r.left >= 0 && r.top >= 0 && r.right <= 1919 && r.bottom <= 1079);
            expect(r.left == (1919 - (r.right - r.left)) / 2 && r.top == (1079 - (r.bottom - r.top)) / 2);
        }
        const VideoRect output{1920, 0, 3840, 1080}, game{0, 0, 1920, 1080}, hud{1400, 10, 1900, 500};
        expect(separate_video_output(output, game, hud, true, false));
        expect(!separate_video_output(game, game, hud, true, false));
        expect(!separate_video_output(output, game, hud, false, false));
        expect(!separate_video_output(output, game, hud, true, true));
        expect(!separate_video_output(output, {1500, 0, 2200, 1000}, hud, true, false));
        expect(!separate_video_output(output, game, {1700, 10, 2100, 400}, true, false));
        expect(!separate_video_output({0,0,0,0}, game, hud, true, false));
        expect(separate_video_output({-1920,0,0,1080}, game, hud, true, false));
        std::cout << "video layout: " << assertions << " assertions passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
