// Position figures: camera_to_center / camera_from_center and orbit, on a mock and on tello-sim.

#include "cli/command_processor.hpp"
#include "mock_transport.hpp"
#include "simulator.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_transport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using std::chrono::milliseconds;
using tello::test::MockTransport;

class FiguresCli : public ::testing::Test
{
protected:
    std::shared_ptr<MockTransport> mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone{mock};
    std::ostringstream out;
    cli::CommandProcessor cli{drone, out, {"127.0.0.1", 8889, 0, {}, {}}};
};

TEST_F(FiguresCli, CameraToCenterTurnsTheShortWay)
{
    cli.execute_line("takeoff; forward 100");
    mock->requests.clear();
    out.str("");
    EXPECT_TRUE(cli.execute_line("camera_to_center 100 100")); // to the right
    EXPECT_TRUE(cli.execute_line("camera_to_center 0 0"));     // behind
    EXPECT_TRUE(cli.execute_line("camera_to_center 100 -50")); // to the left
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"cw 90", "cw 90", "cw 90"}));
    EXPECT_NE(out.str().find("camera_to_center: heading 270"), std::string::npos) << out.str();
}

TEST_F(FiguresCli, CameraFromCenterLooksAway)
{
    cli.execute_line("takeoff; forward 100");
    mock->requests.clear();
    EXPECT_TRUE(cli.execute_line("camera_from_center 0 0")); // already facing away
    EXPECT_TRUE(cli.execute_line("camera_from_center 100 100"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"ccw 90"}));
    EXPECT_EQ(cli.position()->position().heading_deg, 270);
}

TEST_F(FiguresCli, CameraRefusals)
{
    cli.execute_line("camera_to_center 0 100");
    EXPECT_NE(out.str().find("not in the air"), std::string::npos) << out.str();
    cli.execute_line("takeoff");
    mock->requests.clear();
    out.str("");
    cli.execute_line("camera_to_center 0 0");
    cli.execute_line("camera_to_center 5");
    cli.execute_line("camera_from_center x 1");
    EXPECT_NE(out.str().find("the drone is at that point"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("usage: camera_to_center <x> <y>"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("not a number"), std::string::npos) << out.str();
    EXPECT_TRUE(mock->requests.empty());
}

TEST_F(FiguresCli, OrbitFliesTwelveLegsFacingTheCenter)
{
    cli.execute_line("takeoff");
    mock->requests.clear();
    out.str("");
    EXPECT_TRUE(cli.execute_line("orbit 100"));
    const auto& r = mock->requests;
    ASSERT_EQ(r.size(), 24U);
    EXPECT_EQ(r[0], "go 13 50 0 30"); // a 30-degree chord: in 13, left 50
    EXPECT_EQ(r[1], "cw 30");
    EXPECT_EQ(std::count(r.begin(), r.end(), "cw 30"), 12);
    const auto p = cli.position()->position();
    EXPECT_NEAR(p.x_cm, 0.0, 2.0);
    EXPECT_NEAR(p.y_cm, 0.0, 2.0);
    EXPECT_EQ(p.heading_deg, 0);
    EXPECT_NE(out.str().find("orbit: radius 100 cm around (100, 0), 12 legs"), std::string::npos)
        << out.str();
    EXPECT_NE(out.str().find("orbit: done, 0 cm from the start"), std::string::npos) << out.str();
}

TEST_F(FiguresCli, OrbitCounterClockwiseAtSpeed)
{
    cli.execute_line("takeoff");
    mock->requests.clear();
    EXPECT_TRUE(cli.execute_line("circle 200 ccw 50"));
    ASSERT_EQ(mock->requests.size(), 24U);
    EXPECT_EQ(mock->requests[0], "go 27 -100 0 50");
    EXPECT_EQ(mock->requests[1], "ccw 30");
}

TEST_F(FiguresCli, OrbitRefusals)
{
    cli.execute_line("orbit 100");
    EXPECT_NE(out.str().find("orbit: not in the air"), std::string::npos) << out.str();
    cli.execute_line("takeoff");
    mock->requests.clear();
    out.str("");
    cli.execute_line("orbit 30");
    cli.execute_line("orbit 100 left");
    cli.execute_line("orbit 100 cw 5");
    cli.execute_line("orbit");
    EXPECT_NE(out.str().find("orbit radius must be 50..500 cm, got 30"), std::string::npos);
    EXPECT_NE(out.str().find("orbit speed must be 10..100, got 5"), std::string::npos);
    EXPECT_NE(out.str().find("usage: orbit <radius> [cw|ccw] [speed]"), std::string::npos);
    EXPECT_TRUE(mock->requests.empty());
}

TEST_F(FiguresCli, FailedLegStopsTheOrbitAndTheLine)
{
    cli.execute_line("takeoff");
    mock->requests.clear();
    mock->reply("ok");
    mock->reply("ok");
    mock->reply("error Motor stop");
    cli.execute_line("orbit 100; land");
    EXPECT_EQ(mock->requests.size(), 3U);
    EXPECT_NE(out.str().find("orbit: stopped after 1 of 12 legs"), std::string::npos) << out.str();
}

TEST(FiguresSimTest, OrbitComesBackToTheStart)
{
    tello::sim::SimConfig config;
    config.command_port = 0;
    config.state_port = 0;
    tello::sim::Simulator sim(config);
    auto transport = std::make_shared<tello::UdpTransport>("127.0.0.1", sim.command_port());
    tello::TelloClassic drone(transport,
                              tello::DroneTimeouts{milliseconds(1000), milliseconds(500)});
    std::ostringstream out;
    cli::CommandProcessor cli(drone, out, {"127.0.0.1", sim.command_port(), 0, {}, {}});

    ASSERT_TRUE(cli.execute_line("connect; takeoff; forward 100; cw 45"));
    const auto start = sim.model();
    ASSERT_TRUE(cli.execute_line("orbit 150 ccw")) << out.str();
    const auto end = sim.model();
    EXPECT_LT(std::hypot(end.x_cm - start.x_cm, end.y_cm - start.y_cm), 3.0) << out.str();
    EXPECT_EQ(end.yaw, start.yaw);

    ASSERT_TRUE(cli.execute_line("camera_to_center 0 0")) << out.str();
    const double bearing = std::atan2(-end.y_cm, -end.x_cm) * 180.0 / 3.141592653589793;
    const int heading = (sim.model().yaw + 360) % 360;
    EXPECT_NEAR(std::remainder(heading - bearing, 360.0), 0.0, 1.0);
    cli.execute_line("land");
}

} // namespace
