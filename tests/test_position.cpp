// Position: dead reckoning by commands and speed, the CLI's position/return, truth from tello-sim.

#include "cli/command_processor.hpp"
#include "drone/dead_reckoning.hpp"
#include "mock_transport.hpp"
#include "simulator.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_transport.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using drone::DeadReckoning;
using drone::Direction;
using std::chrono::milliseconds;
using tello::test::MockTransport;

TEST(DeadReckoningTest, TakeoffIsTheOrigin)
{
    DeadReckoning dr(80);
    dr.on_move(Direction::Forward, 100);
    dr.on_takeoff();
    const auto p = dr.position();
    EXPECT_DOUBLE_EQ(p.x_cm, 0.0);
    EXPECT_DOUBLE_EQ(p.y_cm, 0.0);
    EXPECT_DOUBLE_EQ(p.z_cm, 80.0);
    EXPECT_EQ(p.heading_deg, 0);
    EXPECT_DOUBLE_EQ(p.error_cm, 0.0);
    EXPECT_EQ(dr.name(), "dead reckoning");
}

TEST(DeadReckoningTest, MovesFollowTheHeading)
{
    DeadReckoning dr(80);
    dr.on_takeoff();
    dr.on_move(Direction::Forward, 100); // (100, 0)
    dr.on_rotate(90);                    // facing right
    dr.on_move(Direction::Forward, 50);  // (100, 50)
    dr.on_move(Direction::Left, 30);     // left of "facing right" is forward: (130, 50)
    dr.on_rotate(-180);                  // facing left
    dr.on_move(Direction::Right, 20);    // (150, 50)
    dr.on_move(Direction::Back, 40);     // (150, 90)
    dr.on_move(Direction::Up, 40);
    dr.on_move(Direction::Down, 20);
    const auto p = dr.position();
    EXPECT_NEAR(p.x_cm, 150.0, 1e-9);
    EXPECT_NEAR(p.y_cm, 90.0, 1e-9);
    EXPECT_DOUBLE_EQ(p.z_cm, 100.0);
    EXPECT_EQ(p.heading_deg, 270);
    EXPECT_NEAR(p.error_cm, DeadReckoning::kCommandError * 240.0, 1e-9);
}

TEST(DeadReckoningTest, GoIsForwardLeftUp)
{
    DeadReckoning dr(80);
    dr.on_takeoff();
    dr.on_go(100, 50, 20);
    const auto p = dr.position();
    EXPECT_NEAR(p.x_cm, 100.0, 1e-9);
    EXPECT_NEAR(p.y_cm, -50.0, 1e-9);
    EXPECT_DOUBLE_EQ(p.z_cm, 100.0);
}

TEST(DeadReckoningTest, LandAndFlip)
{
    DeadReckoning dr(80);
    dr.on_takeoff();
    dr.on_flip();
    EXPECT_DOUBLE_EQ(dr.position().error_cm, DeadReckoning::kFlipError);
    dr.on_land();
    EXPECT_DOUBLE_EQ(dr.position().z_cm, 0.0);
}

TEST(DeadReckoningTest, FreeFlightIntegratesSpeedAndSensorYaw)
{
    DeadReckoning dr(80);
    dr.on_takeoff();
    dr.on_rotate(90);
    dr.on_free_flight();
    drone::MotionSample s;
    s.yaw_deg = 30; // first sample: sensor 30 = our heading 90
    s.height_cm = 120;
    dr.on_motion(s);
    EXPECT_EQ(dr.position().heading_deg, 90);
    EXPECT_DOUBLE_EQ(dr.position().z_cm, 120.0);

    s.forward_cm_s = 50.0;
    s.dt = std::chrono::duration<double>(2.0);
    dr.on_motion(s); // 100 cm forward while facing right
    s.yaw_deg = -60; // turned 90 left: facing forward again
    s.forward_cm_s = 0.0;
    s.right_cm_s = 20.0;
    s.height_cm.reset();
    s.up_cm_s = 10.0;
    dr.on_motion(s); // 40 cm right while facing forward
    const auto p = dr.position();
    EXPECT_EQ(p.heading_deg, 0);
    EXPECT_NEAR(p.x_cm, 0.0, 1e-9);
    EXPECT_NEAR(p.y_cm, 140.0, 1e-9);
    EXPECT_DOUBLE_EQ(p.z_cm, 140.0);
    EXPECT_NEAR(p.error_cm, DeadReckoning::kIntegrationError * 140.0, 1e-9);
}

TEST(DeadReckoningTest, ModelsOfferIt)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    EXPECT_TRUE(drone.supports(drone::Capability::Position));
    const auto source = drone.make_position_source();
    ASSERT_NE(source, nullptr);
    source->on_takeoff();
    EXPECT_DOUBLE_EQ(source->position().z_cm, tello::TelloFamily::kTakeoffHeightCm);
}

class PositionCli : public ::testing::Test
{
protected:
    std::shared_ptr<MockTransport> mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone{mock};
    std::ostringstream out;
    cli::CommandProcessor cli{drone, out, {"127.0.0.1", 8889, 0, {}, {}}};
};

TEST_F(PositionCli, PrintsPosition)
{
    cli.execute_line("takeoff; forward 120; cw 90; forward 40; up 20");
    out.str("");
    cli.execute_line("position");
    EXPECT_EQ(out.str(), "x 120  y 40  z 100 cm  heading 090  ±8 cm (dead reckoning)\n");
    out.str("");
    cli.execute_line("pos");
    EXPECT_NE(out.str().find("x 120"), std::string::npos);
}

TEST_F(PositionCli, ReturnFliesLegsInTheBodyFrame)
{
    cli.execute_line("takeoff; forward 500; forward 230; cw 90; forward 60");
    mock->requests.clear();
    out.str("");
    EXPECT_TRUE(cli.execute_line("return"));
    // Facing right at (730, 60): home is 60 cm behind and 730 cm to the right.
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"back 60", "right 500", "right 230"}));
    EXPECT_NE(out.str().find("return: 732 cm to the takeoff point"), std::string::npos);
    EXPECT_NE(out.str().find("above the takeoff point, 0 cm off"), std::string::npos);
    const auto p = cli.position()->position();
    EXPECT_NEAR(p.x_cm, 0.0, 1e-6);
    EXPECT_NEAR(p.y_cm, 0.0, 1e-6);
}

TEST_F(PositionCli, ReturnKeepsEveryLegFlyable)
{
    cli.execute_line("takeoff; forward 255; forward 255");
    mock->requests.clear();
    cli.execute_line("return");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"back 490", "back 20"}));
}

TEST_F(PositionCli, ReturnRefusals)
{
    cli.execute_line("return");
    EXPECT_NE(out.str().find("return: not in the air"), std::string::npos);
    cli.execute_line("takeoff; up 30");
    mock->requests.clear();
    out.str("");
    cli.execute_line("return");
    EXPECT_NE(out.str().find("already above the takeoff point"), std::string::npos);
    EXPECT_TRUE(mock->requests.empty());
}

TEST_F(PositionCli, FailedLegStopsTheReturnAndTheLine)
{
    cli.execute_line("takeoff; forward 100; right 100");
    mock->requests.clear();
    mock->reply("error Motor stop");
    cli.execute_line("return; land");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"back 100"}));
    EXPECT_NE(out.str().find("return: stopped"), std::string::npos);
}

// The simulator knows where it really is: the estimate must match it, and return must get there.
TEST(PositionSimTest, MatchesTheSimulatorAndReturnsHome)
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

    ASSERT_TRUE(cli.execute_line("connect; takeoff; forward 200; cw 90; forward 150; right 70; "
                                 "ccw 45; back 30; go 50 40 30 20; up 40"));
    ASSERT_EQ(out.str().find("error"), std::string::npos) << out.str();
    auto truth = sim.model();
    auto p = cli.position()->position();
    EXPECT_NEAR(p.x_cm, truth.x_cm, 1.0);
    EXPECT_NEAR(p.y_cm, truth.y_cm, 1.0);
    EXPECT_NEAR(p.z_cm, truth.height_cm, 1.0);
    EXPECT_EQ(p.heading_deg, (truth.yaw + 360) % 360);

    ASSERT_TRUE(cli.execute_line("return"));
    ASSERT_EQ(out.str().find("stopped"), std::string::npos) << out.str();
    truth = sim.model();
    EXPECT_LT(std::hypot(truth.x_cm, truth.y_cm), 2.0) << out.str();
    EXPECT_TRUE(truth.flying);
    cli.execute_line("land");
    EXPECT_FALSE(sim.model().flying);
}

} // namespace
