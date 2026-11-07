// Joystick mode: gamepad sticks and buttons in the (fake) video window become rc and flight
// commands.

#include "cli/command_processor.hpp"
#include "cli/config.hpp"
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

video::Gamepad pad(double lx, double ly, double rx, double ry)
{
    video::Gamepad g;
    g.name = "Test Pad";
    g.left_x = lx;
    g.left_y = ly;
    g.right_x = rx;
    g.right_y = ry;
    return g;
}

TEST(GamepadSticksTest, Mode2WithDeadZone)
{
    EXPECT_EQ(cli::sticks_from_gamepad(pad(0, 0, 0, 0), 10), (cli::RcSticks{0, 0, 0, 0}));
    EXPECT_EQ(cli::sticks_from_gamepad(pad(0.05, -0.1, 0.09, 0.1), 10),
              (cli::RcSticks{0, 0, 0, 0}));
    EXPECT_EQ(cli::sticks_from_gamepad(pad(-1, 1, 1, -1), 10),
              (cli::RcSticks{100, -100, 100, -100}));
    EXPECT_EQ(cli::sticks_from_gamepad(pad(0, 0.55, 0, 0), 10), (cli::RcSticks{0, 0, 50, 0}));
    EXPECT_EQ(cli::sticks_from_gamepad(pad(0, 0, -0.5, 0), 0), (cli::RcSticks{-50, 0, 0, 0}));
    EXPECT_EQ(cli::sticks_from_gamepad(pad(1.2, 0, 0, 0), 10), (cli::RcSticks{0, 0, 0, 100}));
}

class JoystickCli : public ::testing::Test
{
protected:
    JoystickCli()
    {
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
    bool interrupted = false;
    int alive = 0;
    FakeViewer* viewer = nullptr;
    std::function<void(FakeViewer&)> script;
    cli::CommandProcessor cli{drone,
                              out,
                              {"127.0.0.1", 8889, 0, {}, {}},
                              nullptr,
                              [](std::chrono::milliseconds)
                              {
                              },
                              [this]
                              {
                                  return interrupted;
                              }};
};

TEST_F(JoystickCli, TakeoffFlyLandFromThePad)
{
    script = [](FakeViewer& v)
    {
        switch (v.polls)
        {
        case 1:
            v.presses = {"escape"}; // typed before the mode: dropped
            break;
        case 2:
            v.pad = pad(0, 0, 0, 1); // on the ground: sticks do nothing
            break;
        case 3:
            v.button_presses = {"a"};
            break;
        case 4:
            v.pad = pad(0.5, 0, 0, 1);
            break;
        case 5:
            v.pad = pad(0, 0, 0, 0);
            v.button_presses = {"b"};
            break;
        default:
            v.presses = {"escape"};
            break;
        }
    };
    EXPECT_TRUE(cli.execute_line("js"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"streamon", "takeoff", "land"}));
    EXPECT_EQ(mock->sent, (std::vector<std::string>{"rc 0 100 0 44", "rc 0 0 0 0"}));
    EXPECT_FALSE(cli.airborne());
    EXPECT_NE(out.str().find("gamepad: Test Pad"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("command mode"), std::string::npos);
    EXPECT_EQ(alive, 1);
}

TEST_F(JoystickCli, EmergencyNeedsBothButtons)
{
    cli.execute_line("takeoff");
    script = [](FakeViewer& v)
    {
        v.pad = pad(0, 0, 0, 0);
        switch (v.polls)
        {
        case 2:
            v.pad->buttons = {"back"};
            v.button_presses = {"back"}; // alone: nothing
            break;
        case 3:
            v.pad->buttons = {"back", "start"};
            v.button_presses = {"start"};
            break;
        case 4:
            v.button_presses = {"guide"};
            break;
        default:
            break;
        }
    };
    cli.execute_line("joystick");
    EXPECT_EQ(std::count(mock->requests.begin(), mock->requests.end(), "emergency"), 1);
    EXPECT_FALSE(cli.airborne());
    EXPECT_NE(out.str().find("command mode"), std::string::npos);
}

TEST_F(JoystickCli, HungWindowHoversWithTheStickHeld)
{
    cli.execute_line("takeoff");
    script = [this](FakeViewer& v)
    {
        v.pad = pad(0, 0, 0, 1);
        if (v.polls == 3)
        {
            v.age = cli::kStaleInput + std::chrono::milliseconds(1);
        }
        if (v.polls == 4)
        {
            interrupted = true;
        }
    };
    cli.execute_line("joystick");
    EXPECT_EQ(mock->sent,
              (std::vector<std::string>{"rc 0 100 0 0", "rc 0 0 0 0", "rc 0 0 0 0", "rc 0 0 0 0"}));
    EXPECT_NE(out.str().find("video window not responding: hovering"), std::string::npos)
        << out.str();
}

TEST_F(JoystickCli, WaitsForAPadAndHoversWhenItIsGone)
{
    cli.execute_line("takeoff");
    script = [this](FakeViewer& v)
    {
        switch (v.polls)
        {
        case 3:
            v.pad = pad(0, 0, 0, 1);
            break;
        case 4:
            v.pad.reset();
            break;
        case 6:
            interrupted = true;
            break;
        default:
            break;
        }
    };
    cli.execute_line("joystick");
    EXPECT_EQ(mock->sent, (std::vector<std::string>{"rc 0 100 0 0", "rc 0 0 0 0", "rc 0 0 0 0"}));
    const std::string text = out.str();
    EXPECT_EQ(text.find("no gamepad"), text.rfind("no gamepad")) << text; // said once
    EXPECT_NE(text.find("gamepad disconnected, hovering"), std::string::npos) << text;
    EXPECT_NE(text.find("interrupted"), std::string::npos) << text;
}

TEST_F(JoystickCli, OsdShowsTheMode)
{
    std::string mode_inside;
    script = [&mode_inside](FakeViewer& v)
    {
        mode_inside = v.sources.osd().mode;
        v.presses = {"escape"};
    };
    cli.execute_line("joystick");
    EXPECT_EQ(mode_inside, "JOYSTICK");
    EXPECT_EQ(viewer->sources.osd().mode, "");
}

TEST_F(JoystickCli, DeadZoneSetting)
{
    cli.set_joystick_deadzone(50);
    cli.execute_line("takeoff");
    script = [](FakeViewer& v)
    {
        v.pad = pad(0, 0, 0.4, 0.75);
        if (v.polls > 2)
        {
            v.presses = {"escape"};
        }
    };
    cli.execute_line("joystick");
    EXPECT_EQ(mock->sent, (std::vector<std::string>{"rc 0 50 0 0", "rc 0 0 0 0"}));
}

TEST(JoystickConfigTest, DeadZoneKey)
{
    std::istringstream in("js_deadzone = 20\njs_deadzone = 80\n");
    const auto loaded = cli::parse_config(in, "config");
    EXPECT_EQ(loaded.config.joystick_deadzone, 20);
    ASSERT_EQ(loaded.warnings.size(), 1U);
    EXPECT_NE(loaded.warnings[0].find("0..50"), std::string::npos);
}

} // namespace
