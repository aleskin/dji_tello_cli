// Keyboard mode: keys held in the (fake) video window become rc commands at 20 Hz.

#include "cli/command_processor.hpp"
#include "fake_viewer.hpp"
#include "mock_transport.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_socket.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

using tello::test::FakeViewer;
using tello::test::MockTransport;

class KeyboardCli : public ::testing::Test
{
protected:
    KeyboardCli()
    {
        // The window's stream reads from an ephemeral port nobody sends to.
        const tello::UdpSocket probe("127.0.0.1", 0);
        drone.set_video_port(probe.local_port());
        cli.set_viewer_factory(
            [this](video::ViewerSources s) -> std::unique_ptr<video::Viewer>
            {
                auto v = std::make_unique<FakeViewer>(std::move(s), alive);
                viewer = v.get();
                v->on_poll = [this](FakeViewer& fv)
                {
                    if (script)
                    {
                        script(fv);
                    }
                };
                return v;
            });
    }

    std::shared_ptr<MockTransport> mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone{mock};
    std::ostringstream out;
    std::chrono::milliseconds slept{0};
    bool interrupted = false;
    int alive = 0;
    FakeViewer* viewer = nullptr;
    std::function<void(FakeViewer&)> script;
    cli::CommandProcessor cli{drone,
                              out,
                              {"127.0.0.1", 8889, 0, {}, {}},
                              nullptr,
                              [this](std::chrono::milliseconds d)
                              {
                                  slept += d;
                              },
                              [this]
                              {
                                  return interrupted;
                              }};
};

TEST(SticksTest, KeysToRcAxes)
{
    const cli::KeyLayout layout;
    EXPECT_EQ(cli::sticks_from_keys({}, layout, 40), (cli::RcSticks{0, 0, 0, 0}));
    EXPECT_EQ(cli::sticks_from_keys({"w", "d"}, layout, 40), (cli::RcSticks{40, 40, 0, 0}));
    EXPECT_EQ(cli::sticks_from_keys({"s", "a", "down", "left"}, layout, 30),
              (cli::RcSticks{-30, -30, -30, -30}));
    EXPECT_EQ(cli::sticks_from_keys({"up", "right"}, layout, 100), (cli::RcSticks{0, 0, 100, 100}));
    EXPECT_EQ(cli::sticks_from_keys({"w", "s"}, layout, 40), (cli::RcSticks{0, 0, 0, 0}));
    EXPECT_EQ(cli::sticks_from_keys({"x", "space"}, layout, 40), (cli::RcSticks{0, 0, 0, 0}));
}

TEST(SticksTest, CustomLayout)
{
    cli::KeyLayout layout;
    layout.forward = "i";
    EXPECT_EQ(cli::sticks_from_keys({"i"}, layout, 50), (cli::RcSticks{0, 50, 0, 0}));
    EXPECT_EQ(cli::sticks_from_keys({"w"}, layout, 50), (cli::RcSticks{0, 0, 0, 0}));
}

TEST_F(KeyboardCli, RefusedOnTheGround)
{
    EXPECT_TRUE(cli.execute_line("keyboard"));
    EXPECT_NE(out.str().find("take off first"), std::string::npos) << out.str();
    EXPECT_TRUE(mock->sent.empty());
    EXPECT_EQ(alive, 0);
}

TEST_F(KeyboardCli, KeysBecomeRcUntilEscape)
{
    cli.execute_line("takeoff");
    script = [](FakeViewer& v)
    {
        switch (v.polls)
        {
        case 1:
            v.presses = {"escape"}; // typed before the mode: dropped
            break;
        case 2:
            v.pressed = {"w"};
            break;
        case 3:
            v.pressed = {"w", "left"};
            break;
        case 4:
            v.pressed = {};
            break;
        default:
            v.presses = {"escape"};
            break;
        }
    };
    EXPECT_TRUE(cli.execute_line("kb"));
    EXPECT_EQ(mock->sent, (std::vector<std::string>{"rc 0 40 0 0", "rc 0 40 0 -40", "rc 0 0 0 0",
                                                    "rc 0 0 0 0"}));
    EXPECT_EQ(slept, 3 * cli::kKeyboardPeriod);
    EXPECT_NE(out.str().find("keyboard mode"), std::string::npos);
    EXPECT_NE(out.str().find("command mode"), std::string::npos);
    EXPECT_EQ(alive, 1); // the window stays open after the mode
    EXPECT_TRUE(cli.airborne());
}

TEST_F(KeyboardCli, OsdShowsTheModeOnlyWhileActive)
{
    cli.execute_line("takeoff");
    std::string mode_inside;
    script = [&mode_inside](FakeViewer& v)
    {
        mode_inside = v.sources.osd().mode;
        v.presses = {"escape"};
    };
    cli.execute_line("keyboard");
    EXPECT_EQ(mode_inside, "KEYBOARD");
    EXPECT_EQ(viewer->sources.osd().mode, "");
}

TEST_F(KeyboardCli, SpeedSetting)
{
    cli.execute_line("kbspeed");
    EXPECT_EQ(out.str(), "keyboard speed 40\n");
    out.str("");
    cli.execute_line("kbspeed 70");
    cli.execute_line("kbspeed 5");
    cli.execute_line("kbspeed fast");
    EXPECT_NE(out.str().find("keyboard speed 70"), std::string::npos);
    EXPECT_NE(out.str().find("kbspeed must be 10..100, got 5"), std::string::npos);
    EXPECT_NE(out.str().find("not a number"), std::string::npos);

    cli.execute_line("takeoff");
    script = [](FakeViewer& v)
    {
        v.pressed = {"d"};
        if (v.polls > 2)
        {
            v.presses = {"escape"};
        }
    };
    cli.execute_line("keyboard");
    EXPECT_EQ(mock->sent, (std::vector<std::string>{"rc 70 0 0 0", "rc 0 0 0 0"}));
}

TEST_F(KeyboardCli, ClosingTheWindowStopsTheMode)
{
    cli.execute_line("takeoff");
    script = [](FakeViewer& v)
    {
        v.pressed = {"w"};
        if (v.polls > 2)
        {
            v.open = false;
        }
    };
    cli.execute_line("keyboard");
    EXPECT_EQ(mock->sent.back(), "rc 0 0 0 0");
    EXPECT_NE(out.str().find("window closed"), std::string::npos) << out.str();
}

TEST_F(KeyboardCli, CtrlCStopsAndLands)
{
    cli.execute_line("takeoff");
    script = [this](FakeViewer& v)
    {
        v.pressed = {"w"};
        if (v.polls > 3)
        {
            interrupted = true;
        }
    };
    cli.execute_line("keyboard; forward 100");
    EXPECT_EQ(mock->sent.back(), "rc 0 0 0 0");
    EXPECT_EQ(std::count(mock->requests.begin(), mock->requests.end(), "forward 100"), 0);
    cli.on_interrupt();
    EXPECT_EQ(mock->requests.back(), "land");
}

TEST_F(KeyboardCli, NoWindowSupport)
{
    cli.set_viewer_factory({});
    cli.execute_line("takeoff");
    cli.execute_line("keyboard");
    EXPECT_NE(out.str().find("no window support"), std::string::npos) << out.str();
    EXPECT_TRUE(mock->sent.empty());
}

} // namespace
