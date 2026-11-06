// Simulator logic without sockets, then the real UDP stack against tello-sim on 127.0.0.1.

#include "drone/errors.hpp"
#include "simulator.hpp"
#include "tello/errors.hpp"
#include "tello/state_listener.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_transport.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

namespace tello::sim
{
namespace
{

using std::chrono::milliseconds;
using std::chrono::seconds;

class SimLogicTest : public ::testing::Test
{
protected:
    std::optional<std::string> cmd(std::string_view c)
    {
        return logic.handle(c, now);
    }
    void advance(Clock::duration d)
    {
        now += d;
        logic.tick(now);
    }

    SimConfig config;
    SimLogic logic{config};
    Clock::time_point now = Clock::now();
};

TEST_F(SimLogicTest, SilentUntilSdkMode)
{
    EXPECT_FALSE(cmd("takeoff").has_value());
    EXPECT_EQ(cmd("command"), "ok");
    EXPECT_EQ(cmd("takeoff"), "ok");
}

TEST_F(SimLogicTest, FlightCycle)
{
    cmd("command");
    EXPECT_EQ(cmd("land"), "error Not flying");
    EXPECT_EQ(cmd("up 50"), "error Not flying");
    EXPECT_EQ(cmd("takeoff"), "ok");
    EXPECT_EQ(cmd("up 50"), "ok");
    EXPECT_EQ(cmd("height?"), "13dm");
    EXPECT_EQ(cmd("cw 90"), "ok");
    EXPECT_EQ(cmd("forward 100"), "ok");
    EXPECT_NEAR(logic.model().y_cm, 100.0, 1e-6); // facing right after cw 90
    EXPECT_NEAR(logic.model().x_cm, 0.0, 1e-6);
    EXPECT_EQ(cmd("land"), "ok");
    EXPECT_FALSE(logic.model().flying);
}

TEST_F(SimLogicTest, RangeErrors)
{
    cmd("command");
    cmd("takeoff");
    EXPECT_EQ(cmd("forward 19"), "out of range");
    EXPECT_EQ(cmd("forward 501"), "out of range");
    EXPECT_EQ(cmd("forward"), "out of range");
    EXPECT_EQ(cmd("cw 0"), "out of range");
    EXPECT_EQ(cmd("flip x"), "out of range");
    EXPECT_EQ(cmd("speed 5"), "out of range");
    EXPECT_EQ(cmd("go 10 10 10 50"), "out of range");
    EXPECT_EQ(cmd("selfie"), "unknown command: selfie");
}

TEST_F(SimLogicTest, RcHasNoReply)
{
    cmd("command");
    EXPECT_FALSE(cmd("rc 0 0 0 0").has_value());
}

TEST_F(SimLogicTest, BatteryDrainsInFlightAndBlocksFlip)
{
    config.battery = 55.0;
    config.drain_per_minute = 60.0; // 1 % per second
    SimLogic low{config};
    low.handle("command", now);
    low.handle("takeoff", now);
    EXPECT_EQ(low.handle("flip l", now), "ok");
    now += seconds(10);
    EXPECT_EQ(low.handle("flip l", now), "error Battery low"); // 45 %
    EXPECT_EQ(low.handle("battery?", now), "45");
}

TEST_F(SimLogicTest, AutoLandsAfterSilence)
{
    cmd("command");
    cmd("takeoff");
    advance(seconds(14));
    EXPECT_TRUE(logic.model().flying);
    advance(seconds(2));
    EXPECT_FALSE(logic.model().flying);
    EXPECT_EQ(logic.model().height_cm, 0);
}

TEST_F(SimLogicTest, PositionAndHeading)
{
    cmd("command");
    cmd("takeoff");
    EXPECT_EQ(cmd("ccw 90"), "ok");
    EXPECT_EQ(logic.model().yaw, -90);
    EXPECT_EQ(cmd("forward 50"), "ok"); // facing left
    EXPECT_NEAR(logic.model().y_cm, -50.0, 1e-6);
    EXPECT_EQ(cmd("cw 360"), "ok");
    EXPECT_EQ(logic.model().yaw, -90);
    EXPECT_EQ(cmd("down 500"), "ok");
    EXPECT_EQ(logic.model().height_cm, 20); // never below the minimum hover height
}

TEST_F(SimLogicTest, LandsWhenBatteryRunsOut)
{
    config.battery = 11.0;
    config.drain_per_minute = 600.0; // 10 % per second
    SimLogic low{config};
    low.handle("command", now);
    low.handle("takeoff", now);
    now += milliseconds(500);
    low.handle("battery?", now);
    EXPECT_TRUE(low.model().flying);
    now += seconds(1);
    low.handle("battery?", now);
    EXPECT_FALSE(low.model().flying);
    EXPECT_EQ(low.handle("takeoff", now), "error Battery low");
}

TEST_F(SimLogicTest, ClassicModelLacksSdk2Commands)
{
    cmd("command");
    EXPECT_EQ(cmd("stop"), "unknown command: stop");
    EXPECT_EQ(cmd("sdk?"), "unknown command: sdk?");
    EXPECT_EQ(cmd("sn?"), "unknown command: sn?");
    EXPECT_EQ(cmd("battery?"), "100");
    EXPECT_EQ(logic.telemetry().find("mid:"), std::string::npos);
    EXPECT_TRUE(parse_state(logic.telemetry()).has_value());
}

TEST_F(SimLogicTest, EduModelHasSdk2Commands)
{
    config.model = SimModelKind::TelloEdu;
    SimLogic edu{config};
    edu.handle("command", now);
    EXPECT_EQ(edu.handle("stop", now), "ok");
    EXPECT_EQ(edu.handle("sdk?", now), "20");
    EXPECT_EQ(edu.telemetry().rfind("mid:-1;", 0), 0u);
}

TEST(SimModelTest, ParsesFactoryNames)
{
    EXPECT_EQ(parse_sim_model("tello"), SimModelKind::Tello);
    EXPECT_EQ(parse_sim_model("tello-edu"), SimModelKind::TelloEdu);
    EXPECT_FALSE(parse_sim_model("mavic").has_value());
}

TEST_F(SimLogicTest, CommandsKeepItInTheAir)
{
    cmd("command");
    cmd("takeoff");
    for (int i = 0; i < 5; ++i)
    {
        advance(seconds(10));
        cmd("battery?");
    }
    EXPECT_TRUE(logic.model().flying);
}

// --- Real sockets: UdpTransport + Drone + StateListener against Simulator. ---

class SimulatorUdpTest : public ::testing::Test
{
protected:
    SimulatorUdpTest()
        : listener("127.0.0.1", 0)
        , sim(make_config(listener.local_port()))
        , transport(std::make_shared<UdpTransport>("127.0.0.1", sim.command_port()))
        , drone(transport, DroneTimeouts{milliseconds(1000), milliseconds(500)})
    {
    }

    static SimConfig make_config(std::uint16_t state_port,
                                 SimModelKind model = SimModelKind::TelloEdu)
    {
        SimConfig c;
        c.model = model;
        c.command_port = 0; // ephemeral: tests never use fixed ports
        c.state_port = state_port;
        c.state_period = milliseconds(20);
        return c;
    }

    StateListener listener;
    Simulator sim;
    std::shared_ptr<UdpTransport> transport;
    TelloEdu drone;
};

TEST_F(SimulatorUdpTest, FlyOverUdp)
{
    drone.connect();
    drone.takeoff();
    drone.move(Direction::Up, 100);
    EXPECT_EQ(drone.height_cm(), 180);
    EXPECT_EQ(drone.battery(), 100);
    EXPECT_EQ(transport->request("takeoff", milliseconds(500)), "error Already flying");
    EXPECT_THROW(drone.takeoff(), TelloError);
    drone.land();
    EXPECT_FALSE(sim.model().flying);
}

TEST_F(SimulatorUdpTest, ErrorReplyBecomesException)
{
    drone.connect();
    EXPECT_THROW(drone.land(), TelloError); // not flying
}

TEST_F(SimulatorUdpTest, NoReplyBeforeSdkModeTimesOut)
{
    EXPECT_THROW(drone.takeoff(), drone::NoReply);
    EXPECT_FALSE(sim.model().flying);
}

TEST_F(SimulatorUdpTest, QueriesOverUdp)
{
    drone.connect();
    drone.set_speed(50);
    EXPECT_EQ(drone.speed(), 50);
    EXPECT_EQ(drone.sdk_version(), "20");
    EXPECT_EQ(drone.flight_time(), "0s");
    drone.takeoff();
    drone.rotate_cw(90);
    EXPECT_EQ(drone.attitude(), "pitch:0;roll:0;yaw:90;");
    drone.go(100, 0, 0, 50);
    EXPECT_NEAR(sim.model().y_cm, 100.0, 1e-6);
    drone.rc(0, 0, 0, 0); // no reply expected, must not disturb the next request
    drone.emergency();
    EXPECT_FALSE(sim.model().flying);
}

TEST_F(SimulatorUdpTest, OutOfRangeFromSimulatorIsAnError)
{
    drone.connect();
    drone.takeoff();
    EXPECT_EQ(transport->request("forward 600", milliseconds(500)), "out of range");
}

TEST_F(SimulatorUdpTest, TelemetryArrives)
{
    drone.connect();
    drone.takeoff();
    const auto deadline = std::chrono::steady_clock::now() + seconds(2);
    std::optional<StateSnapshot> snapshot;
    while (std::chrono::steady_clock::now() < deadline)
    {
        snapshot = listener.latest();
        if (snapshot && snapshot->state.h == 80)
        {
            break;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->state.h, 80);
    EXPECT_EQ(snapshot->state.bat, 100);
}

// The owner's drone: classic model against the classic simulator, through the base class only.
TEST(SimulatorClassicTest, ClassicModelOverUdp)
{
    StateListener listener("127.0.0.1", 0);
    SimConfig config;
    config.command_port = 0;
    config.state_port = listener.local_port();
    config.state_period = milliseconds(20);
    Simulator sim(config);
    auto transport = std::make_shared<UdpTransport>("127.0.0.1", sim.command_port());
    std::unique_ptr<drone::DroneBase> drone = std::make_unique<TelloClassic>(
        transport, DroneTimeouts{milliseconds(1000), milliseconds(500)});

    drone->connect();
    drone->takeoff();
    drone->move(Direction::Up, 50);
    EXPECT_EQ(drone->height_cm(), 130);
    EXPECT_THROW(drone->hover(), drone::NotSupported); // refused locally, never sent
    EXPECT_EQ(transport->request("stop", milliseconds(500)), "unknown command: stop");
    drone->land();
    EXPECT_FALSE(sim.model().flying);

    const auto deadline = std::chrono::steady_clock::now() + seconds(2);
    while (!listener.latest() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(milliseconds(10));
    }
    EXPECT_TRUE(listener.latest().has_value()); // SDK 1.3 telemetry parses too
}

} // namespace
} // namespace tello::sim
