// Session log (TEL-00056): file format, rotation, scopes, and what a CLI session writes.

#include "applog/log.hpp"
#include "cli/command_processor.hpp"
#include "cli/flight_log.hpp"
#include "simulator.hpp"
#include "tello/errors.hpp"
#include "tello/state_listener.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_transport.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{

using std::chrono::milliseconds;

class LogTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        static int counter = 0;
        dir = std::filesystem::temp_directory_path() /
              ("tello_log_test_" + std::to_string(::getpid()) + "_" + std::to_string(++counter));
        std::filesystem::remove_all(dir);
    }

    void TearDown() override
    {
        applog::close();
        std::filesystem::remove_all(dir);
    }

    [[nodiscard]] std::string text() const
    {
        std::ifstream in(path);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }

    [[nodiscard]] bool has(const std::string& part) const
    {
        return text().find(part) != std::string::npos;
    }

    std::filesystem::path dir;
    std::filesystem::path path;
};

TEST_F(LogTest, RecordFormat)
{
    path = applog::open(dir);
    EXPECT_EQ(applog::current(), path);
    EXPECT_TRUE(
        std::regex_match(path.filename().string(), std::regex(R"(tello_\d{8}_\d{6}(_\d+)?\.log)")));
    applog::set_thread_name("main");
    applog::info("drone", "> takeoff");
    applog::error("cli", "two\nlines\r\n");
    const std::string all = text();
    const std::regex line(
        R"(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} INFO  main     drone: > takeoff\n)"
        R"(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} ERROR main     cli: two \| lines\n)");
    EXPECT_TRUE(std::regex_match(all, line)) << all;
}

TEST_F(LogTest, ClosedLogWritesNothing)
{
    path = applog::open(dir);
    applog::close();
    EXPECT_TRUE(applog::current().empty());
    applog::info("cli", "lost");
    EXPECT_FALSE(has("lost"));
}

TEST_F(LogTest, KeepsOnlyNewestFiles)
{
    for (int i = 0; i < 5; ++i)
    {
        path = applog::open(dir, 3);
    }
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        (void)entry;
        ++count;
    }
    EXPECT_EQ(count, 3);
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST_F(LogTest, ScopeLogsHowItEnded)
{
    path = applog::open(dir);
    {
        const applog::Scope ok("cli", "'battery'");
    }
    {
        applog::Scope failed("cli", "'up 5'");
        failed.fail("bad distance");
    }
    try
    {
        const applog::Scope thrown("cli", "'boom'");
        throw std::runtime_error("boom");
    }
    catch (const std::runtime_error&)
    {
    }
    EXPECT_TRUE(has("DEBUG"));
    EXPECT_TRUE(has("begin 'battery'"));
    EXPECT_TRUE(std::regex_search(text(), std::regex(R"(INFO .* end 'battery': ok, \d+ ms)")));
    EXPECT_TRUE(std::regex_search(
        text(), std::regex(R"(WARN .* end 'up 5': failed: bad distance, \d+ ms)")));
    EXPECT_TRUE(std::regex_search(text(), std::regex(R"(ERROR .* end 'boom': exception, \d+ ms)")));
}

TEST_F(LogTest, TransportLogsRequestsAndCollapsesRc)
{
    path = applog::open(dir);
    tello::sim::SimConfig config;
    config.command_port = 0;
    config.state_port = 0;
    tello::sim::Simulator sim(config);
    tello::UdpTransport transport("127.0.0.1", sim.command_port());
    EXPECT_EQ(transport.request("command", milliseconds(1000)), "ok");
    for (int i = 0; i < 5; ++i)
    {
        transport.send("rc 0 10 0 0");
    }
    transport.send("rc 0 0 0 0");
    (void)transport.request("battery?", milliseconds(1000));
    EXPECT_TRUE(has("drone: > command\n"));
    EXPECT_TRUE(std::regex_search(text(), std::regex(R"(drone: < ok  \[command, \d+ ms\])")));
    EXPECT_TRUE(has("> rc 0 10 0 0 (no reply)"));
    EXPECT_TRUE(has("(rc 0 10 0 0 repeated 4 times)"));
    EXPECT_TRUE(has("> rc 0 0 0 0 (no reply)"));
}

TEST_F(LogTest, TransportLogsTimeout)
{
    path = applog::open(dir);
    tello::sim::SimConfig config;
    config.command_port = 0;
    config.state_port = 0;
    config.drop_percent = 100;
    tello::sim::Simulator sim(config);
    tello::UdpTransport transport("127.0.0.1", sim.command_port());
    EXPECT_THROW((void)transport.request("command", milliseconds(100)), drone::NoReply);
    EXPECT_TRUE(std::regex_search(text(), std::regex(R"(WARN .* ! no reply to 'command' after)")));
}

TEST_F(LogTest, CliSessionIsTraceable)
{
    path = applog::open(dir);
    tello::sim::SimConfig config;
    config.command_port = 0;
    config.state_port = 0;
    tello::sim::Simulator sim(config);
    tello::TelloClassic drone(
        std::make_shared<tello::UdpTransport>("127.0.0.1", sim.command_port()),
        tello::DroneTimeouts{milliseconds(1000), milliseconds(500)});
    std::ostringstream out;
    cli::CommandProcessor cli(
        drone, out, cli::SessionInfo{"127.0.0.1", sim.command_port(), 0, dir, dir}, nullptr,
        [](milliseconds)
        {
        });
    cli.execute_line("connect; takeoff; up 5");
    cli.execute_line("hello");
    cli.execute_line("land");
    const std::string all = text();
    EXPECT_TRUE(has("cli: line: connect; takeoff; up 5"));
    EXPECT_TRUE(has("drone: > takeoff"));
    EXPECT_TRUE(std::regex_search(all, std::regex(R"(end 'takeoff': ok, \d+ ms)")));
    EXPECT_TRUE(std::regex_search(all, std::regex(R"(position x=0 y=0 z=\d+ .*\(in the air\))")));
    EXPECT_TRUE(std::regex_search(all, std::regex(R"(WARN .* end 'up 5': failed: )")));
    EXPECT_TRUE(has("unknown command 'hello'"));
    EXPECT_TRUE(has("(landed)"));
}

TEST_F(LogTest, CliErrorReplyIsAnError)
{
    path = applog::open(dir);
    tello::sim::SimConfig config;
    config.command_port = 0;
    config.state_port = 0;
    config.fail_commands = {"takeoff"};
    tello::sim::Simulator sim(config);
    tello::TelloClassic drone(
        std::make_shared<tello::UdpTransport>("127.0.0.1", sim.command_port()),
        tello::DroneTimeouts{milliseconds(1000), milliseconds(500)});
    std::ostringstream out;
    cli::CommandProcessor cli(
        drone, out, cli::SessionInfo{"127.0.0.1", sim.command_port(), 0, dir, dir}, nullptr,
        [](milliseconds)
        {
        });
    cli.execute_line("connect; takeoff");
    EXPECT_TRUE(std::regex_search(text(), std::regex(R"(ERROR .* cli: takeoff: )")));
    EXPECT_TRUE(std::regex_search(text(), std::regex(R"(WARN .* end 'takeoff': failed: )")));
}

TEST_F(LogTest, TelemetrySnapshotsAndLoss)
{
    path = applog::open(dir);
    tello::StateListener listener("127.0.0.1", 0);
    tello::sim::SimConfig config;
    config.command_port = 0;
    config.state_port = listener.local_port();
    config.state_period = milliseconds(20);
    auto sim = std::make_unique<tello::sim::Simulator>(config);
    tello::UdpTransport transport("127.0.0.1", sim->command_port());
    (void)transport.request("command", milliseconds(1000));
    {
        const cli::TelemetryLog telemetry(
            listener,
            []
            {
                return true;
            },
            cli::TelemetryLogSettings{milliseconds(50), milliseconds(1000), milliseconds(200),
                                      milliseconds(10)});
        std::this_thread::sleep_for(milliseconds(300));
        sim.reset();
        std::this_thread::sleep_for(milliseconds(400));
    }
    EXPECT_TRUE(std::regex_search(text(), std::regex(R"(telem: bat=100% h=0cm tof=\d+cm)")));
    EXPECT_TRUE(has("telemetry lost: last packet"));
}

TEST(TelemetryDescribeTest, OneLine)
{
    tello::DroneState s;
    s.bat = 87;
    s.h = 120;
    s.yaw = -90;
    s.baro = 12.345;
    EXPECT_EQ(cli::describe(s), "bat=87% h=120cm tof=0cm temp=0..0C v=0,0,0dm/s pitch=0 roll=0 "
                                "yaw=-90 baro=12.35m time=0s");
}

} // namespace
