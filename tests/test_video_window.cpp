// Live window: OSD text and the SDL window itself on SDL's offscreen "dummy" video driver.

#include "video/video_window.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace
{

TEST(OsdTest, TextWithAndWithoutTelemetry)
{
    EXPECT_EQ(video::osd_text({87, 120, 45}), "BAT 87%  H 120CM  HDG 045");
    EXPECT_EQ(video::osd_text({}), "BAT --%  H --CM  HDG --");
}

TEST(OsdTest, HeadingFromYaw)
{
    EXPECT_EQ(video::heading_from_yaw(0), 0);
    EXPECT_EQ(video::heading_from_yaw(90), 90);
    EXPECT_EQ(video::heading_from_yaw(-90), 270);
    EXPECT_EQ(video::heading_from_yaw(-180), 180);
    EXPECT_EQ(video::heading_from_yaw(180), 180);
}

video::Picture grey_picture(std::int64_t number)
{
    video::Picture p;
    p.width = 64;
    p.height = 48;
    p.number = number;
    p.y.assign(64 * 48, 128);
    p.u.assign(32 * 24, 128);
    p.v.assign(32 * 24, 128);
    return p;
}

TEST(VideoWindowTest, ShowsPicturesOffscreen)
{
    ::setenv("SDL_VIDEODRIVER", "dummy", 1); // no display in tests
    std::int64_t produced = 0;
    int osd_calls = 0;
    {
        video::VideoWindow window({[&produced](std::int64_t newer_than)
                                   {
                                       ++produced;
                                       return std::optional<video::Picture>(
                                           grey_picture(newer_than + 1));
                                   },
                                   [&osd_calls]
                                   {
                                       ++osd_calls;
                                       return video::Osd{50, 100, 10};
                                   }});
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (window.pictures_shown() < 5 && window.is_open() &&
               std::chrono::steady_clock::now() < until)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_TRUE(window.is_open()) << window.error();
        EXPECT_GE(window.pictures_shown(), 5);
    }
    EXPECT_GT(osd_calls, 0);
}

TEST(VideoWindowTest, NoPictureYetKeepsWindowOpen)
{
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
    video::VideoWindow window({[](std::int64_t)
                               {
                                   return std::optional<video::Picture>();
                               },
                               {}});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(window.is_open()) << window.error();
    EXPECT_EQ(window.pictures_shown(), 0);
}

TEST(VideoWindowTest, ReportsWhenSdlCannotStart)
{
    ::setenv("SDL_VIDEODRIVER", "no-such-driver", 1);
    video::VideoWindow window({{}, {}});
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (window.is_open() && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_FALSE(window.is_open());
    EXPECT_NE(window.error().find("SDL"), std::string::npos) << window.error();
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
}

} // namespace
