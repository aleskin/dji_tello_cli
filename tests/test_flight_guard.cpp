// FlightGuard: keepalive and low-battery landing, on a mock and against the simulator.

#include "drone/flight_guard.hpp"
#include "mock_transport.hpp"
#include "simulator.hpp"
#include "tello/state_listener.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_transport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

using std::chrono::milliseconds;
using tello::test::MockTransport;

drone::GuardSettings fast()
{
    drone::GuardSettings s;
    s.keepalive_after = milliseconds(100);
    s.check_period = milliseconds(10);
    return s;
}

// Thread-safe sink for guard messages.
struct Messages
{
    std::mutex mutex;
    std::vector<std::string> list;
    drone::FlightGuard::Notify sink()
    {
        return [this](const std::string& m)
        {
            const std::lock_guard lock(mutex);
            list.push_back(m);
        };
    }
    std::size_t count_containing(const std::string& text)
    {
        const std::lock_guard lock(mutex);
        return static_cast<std::size_t>(std::count_if(list.begin(), list.end(),
                                                      [&](const std::string& m)
                                                      {
                                                          return m.find(text) != std::string::npos;
                                                      }));
    }
};

std::size_t count(const std::vector<std::string>& v, const std::string& s)
{
    return static_cast<std::size_t>(std::count(v.begin(), v.end(), s));
}

TEST(FlightGuardTest, KeepaliveWhenIdle)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.connect();
    {
        drone::FlightGuard guard(drone, fast(), {}, {});
        std::this_thread::sleep_for(milliseconds(350));
    }
    EXPECT_GE(count(mock->requests, "command"), 3u); // connect + at least two keepalives
}

TEST(FlightGuardTest, NoKeepaliveBeforeFirstCommand)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    {
        drone::FlightGuard guard(drone, fast(), {}, {});
        std::this_thread::sleep_for(milliseconds(250));
    }
    EXPECT_TRUE(mock->requests.empty());
}

TEST(FlightGuardTest, NoKeepaliveWhileUserIsActive)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.takeoff();
    {
        auto settings = fast();
        settings.keepalive_after = milliseconds(300);
        drone::FlightGuard guard(drone, settings, {}, {});
        std::this_thread::sleep_for(milliseconds(200));
    }
    EXPECT_EQ(count(mock->requests, "command"), 0u);
}

TEST(FlightGuardTest, KeepaliveFailureReportedOncePerOutage)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock, tello::DroneTimeouts{milliseconds(10), milliseconds(10)});
    drone.connect(); // empty queue: "ok"
    for (int i = 0; i < 3; ++i)
    {
        mock->reply(std::nullopt); // the next three keepalives time out
    }
    Messages messages;
    {
        drone::FlightGuard guard(drone, fast(), {}, messages.sink());
        std::this_thread::sleep_for(milliseconds(500));
    }
    EXPECT_EQ(messages.count_containing("keepalive failed"), 1u);
    EXPECT_EQ(messages.count_containing("answers again"), 1u);
}

TEST(FlightGuardTest, LowBatteryWarnsThenLandsOnce)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.takeoff(); // keeps keepalive quiet for the first 100 ms only; we count "land"
    std::atomic<int> battery{20};
    Messages messages;
    {
        auto settings = fast();
        settings.keepalive_after = milliseconds(60'000);
        drone::FlightGuard guard(
            drone, settings,
            [&battery]
            {
                return std::optional<int>(battery.load());
            },
            messages.sink());
        std::this_thread::sleep_for(milliseconds(60));
        EXPECT_EQ(messages.count_containing("battery"), 0u);
        battery = 14;
        std::this_thread::sleep_for(milliseconds(60));
        EXPECT_EQ(messages.count_containing("battery low: 14 %"), 1u);
        battery = 9;
        std::this_thread::sleep_for(milliseconds(100));
    }
    EXPECT_EQ(messages.count_containing("battery low"), 1u);
    EXPECT_EQ(messages.count_containing("landing"), 1u);
    EXPECT_EQ(count(mock->requests, "land"), 1u);
}

TEST(FlightGuardTest, UnknownBatteryDoesNothing)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    {
        auto settings = fast();
        settings.keepalive_after = milliseconds(60'000);
        drone::FlightGuard guard(drone, settings,
                                 []
                                 {
                                     return std::optional<int>();
                                 },
                                 {});
        std::this_thread::sleep_for(milliseconds(50));
    }
    EXPECT_EQ(count(mock->requests, "command"), 0u);
    EXPECT_EQ(count(mock->requests, "land"), 0u);
}

// --- Against the simulator (auto-land and battery drain sped up). ---

class GuardSimTest : public ::testing::Test
{
protected:
    void start(tello::sim::SimConfig config)
    {
        config.command_port = 0;
        config.state_port = listener.local_port();
        config.state_period = milliseconds(20);
        sim = std::make_unique<tello::sim::Simulator>(config);
        drone = std::make_unique<tello::TelloClassic>(
            std::make_shared<tello::UdpTransport>("127.0.0.1", sim->command_port()),
            tello::DroneTimeouts{milliseconds(1000), milliseconds(500)});
    }
    std::optional<int> telemetry_battery() const
    {
        const auto snap = listener.latest();
        return snap ? std::optional<int>(snap->state.bat) : std::nullopt;
    }

    tello::StateListener listener{"127.0.0.1", 0};
    std::unique_ptr<tello::sim::Simulator> sim;
    std::unique_ptr<tello::TelloClassic> drone;
};

TEST_F(GuardSimTest, KeepaliveKeepsDroneInTheAir)
{
    tello::sim::SimConfig config;
    config.idle_land = milliseconds(600);
    start(config);
    drone->connect();
    drone->takeoff();
    auto settings = fast();
    settings.keepalive_after = milliseconds(200);
    drone::FlightGuard guard(*drone, settings, {}, {});
    std::this_thread::sleep_for(milliseconds(1500));
    EXPECT_TRUE(sim->model().flying);
}

TEST_F(GuardSimTest, WithoutGuardTheDroneAutoLands)
{
    tello::sim::SimConfig config;
    config.idle_land = milliseconds(300);
    start(config);
    drone->connect();
    drone->takeoff();
    std::this_thread::sleep_for(milliseconds(800));
    EXPECT_FALSE(sim->model().flying);
}

TEST_F(GuardSimTest, LandsOnLowBatteryFromTelemetry)
{
    tello::sim::SimConfig config;
    config.battery = 16.0;
    config.drain_per_minute = 600.0; // 10 % per second
    start(config);
    drone->connect();
    drone->takeoff();
    Messages messages;
    drone::FlightGuard guard(
        *drone, fast(),
        [this]
        {
            return telemetry_battery();
        },
        messages.sink());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (sim->model().flying && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(milliseconds(20));
    }
    EXPECT_FALSE(sim->model().flying);
    EXPECT_GT(sim->model().battery, 0.0); // landed by the guard, not by an empty battery
    EXPECT_EQ(messages.count_containing("landing"), 1u);
    EXPECT_EQ(messages.count_containing("landing failed"), 0u);
}

} // namespace
