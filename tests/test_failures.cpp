// Failure modes: query retries on a mock, then a misbehaving simulator (drops, delays, errors).

#include "cli/command_processor.hpp"
#include "drone/errors.hpp"
#include "mock_transport.hpp"
#include "simulator.hpp"
#include "tello/errors.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_transport.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <sstream>

namespace
{

using std::chrono::milliseconds;
using tello::DroneTimeouts;
using tello::TelloError;
using tello::test::MockTransport;

TEST(QueryRetryTest, LostReplyIsAskedAgain)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    mock->reply(std::nullopt);
    mock->reply("87");
    EXPECT_EQ(drone.battery(), 87);
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"battery?", "battery?"}));
}

TEST(QueryRetryTest, GivesUpAfterRetries)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock); // default: 2 retries
    for (int i = 0; i < 3; ++i)
    {
        mock->reply(std::nullopt);
    }
    EXPECT_THROW((void)drone.battery(), drone::NoReply);
    EXPECT_EQ(mock->requests.size(), 3u);
}

TEST(QueryRetryTest, ErrorReplyIsNotRetried)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    mock->reply("error");
    EXPECT_THROW((void)drone.battery(), TelloError);
    EXPECT_EQ(mock->requests.size(), 1u);
}

TEST(QueryRetryTest, ActionsAreNeverResent)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    mock->reply(std::nullopt);
    EXPECT_THROW(drone.move(drone::Direction::Forward, 100), drone::NoReply);
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"forward 100"}));
}

TEST(SimFailCmdTest, FailingCommandDoesNothing)
{
    tello::sim::SimConfig config;
    config.fail_commands = {"takeoff"};
    tello::sim::SimLogic logic(config);
    const auto now = tello::sim::Clock::now();
    logic.handle("command", now);
    EXPECT_EQ(logic.handle("takeoff", now), "error");
    EXPECT_FALSE(logic.model().flying);
    EXPECT_EQ(logic.handle("battery?", now), "100");
}

class FaultySim : public ::testing::Test
{
protected:
    void start(tello::sim::SimConfig config,
               DroneTimeouts timeouts = {milliseconds(300), milliseconds(100), 2})
    {
        config.command_port = 0;
        config.state_port = 0;
        sim = std::make_unique<tello::sim::Simulator>(config);
        transport = std::make_shared<tello::UdpTransport>("127.0.0.1", sim->command_port());
        drone = std::make_unique<tello::TelloClassic>(transport, timeouts);
    }

    std::unique_ptr<tello::sim::Simulator> sim;
    std::shared_ptr<tello::UdpTransport> transport;
    std::unique_ptr<tello::TelloClassic> drone;
};

TEST_F(FaultySim, AllRepliesLost)
{
    tello::sim::SimConfig config;
    config.drop_percent = 100;
    start(config);
    EXPECT_THROW(drone->connect(), drone::NoReply); // the drone did enter SDK mode, though
    EXPECT_THROW((void)drone->battery(), drone::NoReply);
    EXPECT_EQ(sim->dropped_replies(), 4); // connect once, battery? three times
    EXPECT_THROW(drone->takeoff(), drone::NoReply);
    EXPECT_TRUE(sim->model().flying); // why actions are never resent
}

TEST_F(FaultySim, RetriesRideOverRandomLoss)
{
    tello::sim::SimConfig config;
    config.drop_percent = 50;
    config.seed = 7;
    start(config, {milliseconds(300), milliseconds(80), 8});
    try
    {
        (void)transport->request("command", milliseconds(300));
    }
    catch (const drone::DroneError&)
    {
        // the reply may be lost; the drone has entered SDK mode anyway
    }
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_EQ(drone->battery(), 100);
    }
    EXPECT_GT(sim->dropped_replies(), 0);
}

TEST_F(FaultySim, SlowReplyTimesOut)
{
    tello::sim::SimConfig config;
    config.reply_delay = milliseconds(250);
    start(config, {milliseconds(100), milliseconds(100), 0});
    EXPECT_THROW(drone->connect(), drone::NoReply);
}

TEST_F(FaultySim, SlowReplyWithinTimeoutIsFine)
{
    tello::sim::SimConfig config;
    config.reply_delay = milliseconds(50);
    start(config, {milliseconds(500), milliseconds(500), 0});
    drone->connect();
    EXPECT_EQ(drone->battery(), 100);
}

TEST_F(FaultySim, CliStopsChainOnRefusedCommand)
{
    tello::sim::SimConfig config;
    config.fail_commands = {"flip"};
    start(config);
    std::ostringstream out;
    cli::CommandProcessor cli(*drone, out, {"127.0.0.1", sim->command_port(), 0, {}, {}});
    cli.execute_line("connect; takeoff; flip l; land");
    EXPECT_NE(out.str().find("error: flip l: error"), std::string::npos);
    EXPECT_TRUE(sim->model().flying); // 'land' after the failure was not run
    EXPECT_TRUE(cli.airborne());
}

TEST_F(FaultySim, MissionLandsOnRefusedCommand)
{
    tello::sim::SimConfig config;
    config.fail_commands = {"flip"};
    start(config);
    std::ostringstream out;
    cli::CommandProcessor cli(*drone, out, {"127.0.0.1", sim->command_port(), 0, {}, {}});
    std::istringstream plan("connect\ntakeoff\nflip b\nforward 100\n");
    EXPECT_FALSE(cli.run_mission(plan, "flips"));
    EXPECT_FALSE(sim->model().flying);
    EXPECT_NEAR(sim->model().x_cm, 0.0, 1e-6); // 'forward 100' never ran
}

} // namespace
