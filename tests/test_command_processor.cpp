// CLI command layer over a mock transport: what is sent, what is printed, model differences.

#include "cli/command_processor.hpp"
#include "mock_transport.hpp"
#include "tello/state_listener.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_socket.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>

namespace
{

using tello::test::MockTransport;

template <typename Model> class Fixture
{
public:
    std::shared_ptr<MockTransport> mock = std::make_shared<MockTransport>();
    Model drone{mock};
    std::ostringstream out;
    std::chrono::milliseconds slept{0};
    bool interrupted = false;
    cli::CommandProcessor cli{drone,
                              out,
                              {"127.0.0.1", 8889, 8890, {}, {}},
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

class ClassicCli : public ::testing::Test, public Fixture<tello::TelloClassic>
{
};
class EduCli : public ::testing::Test, public Fixture<tello::TelloEdu>
{
};

TEST_F(ClassicCli, ChainRunsInOrder)
{
    EXPECT_TRUE(cli.execute_line("takeoff; up 50 ;cw 90; land"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "up 50", "cw 90", "land"}));
}

TEST_F(ClassicCli, ChainStopsAtDroneError)
{
    mock->reply("ok");
    mock->reply("error Not joystick");
    EXPECT_TRUE(cli.execute_line("takeoff; forward 100; land"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "forward 100"}));
    EXPECT_NE(out.str().find("error: forward 100: error Not joystick"), std::string::npos);
}

TEST_F(ClassicCli, BadArgumentsSendNothing)
{
    cli.execute_line("forward");
    cli.execute_line("forward ten");
    cli.execute_line("forward 600"); // range is checked by the family before sending
    cli.execute_line("flip x");
    cli.execute_line("go 1 2 3");
    EXPECT_TRUE(mock->requests.empty());
    EXPECT_NE(out.str().find("usage: forward <cm>"), std::string::npos);
    EXPECT_NE(out.str().find("not a number: 'ten'"), std::string::npos);
    EXPECT_NE(out.str().find("forward must be 20..500, got 600"), std::string::npos);
}

TEST_F(ClassicCli, UnknownCommandStopsChain)
{
    cli.execute_line("selfie; takeoff");
    EXPECT_TRUE(mock->requests.empty());
    EXPECT_NE(out.str().find("unknown command 'selfie'"), std::string::npos);
}

TEST_F(ClassicCli, Sdk2CommandsAreHiddenAndRefused)
{
    cli.execute_line("help");
    EXPECT_EQ(out.str().find("serial number"), std::string::npos);
    EXPECT_NE(out.str().find("Commands for Tello (SDK 1.3)"), std::string::npos);
    out.str("");
    cli.execute_line("sn");
    EXPECT_NE(out.str().find("'sn' is not supported by Tello (SDK 1.3)"), std::string::npos);
    EXPECT_TRUE(mock->requests.empty());
    const auto names = cli.completions("s");
    EXPECT_EQ(std::count(names.begin(), names.end(), "sn"), 0);
    EXPECT_EQ(std::count(names.begin(), names.end(), "speed"), 1);
}

TEST_F(EduCli, Sdk2CommandsAvailable)
{
    mock->reply("0TQZK1");
    cli.execute_line("sn?");
    EXPECT_EQ(out.str(), "0TQZK1\n");
    const auto names = cli.completions("s");
    EXPECT_EQ(std::count(names.begin(), names.end(), "sn"), 1);
}

TEST_F(ClassicCli, ReadsPrintValuesWithUnits)
{
    mock->reply("87");
    mock->reply("12dm");
    cli.execute_line("battery; height?");
    EXPECT_EQ(out.str(), "87 %\n120 cm\n");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"battery?", "height?"}));
}

TEST_F(ClassicCli, AliasesMatchRustCommands)
{
    cli.execute_line("rotate_cw 45; rotate_ccw 30");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"cw 45", "ccw 30"}));
}

TEST_F(ClassicCli, WaitAndExit)
{
    EXPECT_TRUE(cli.execute_line("wait 1.5"));
    EXPECT_EQ(slept, std::chrono::milliseconds(1500));
    EXPECT_TRUE(cli.execute_line("wait -1"));
    EXPECT_EQ(slept, std::chrono::milliseconds(1500));
    EXPECT_FALSE(cli.execute_line("land; exit; takeoff"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"land"}));
}

TEST_F(ClassicCli, HelpForOneCommand)
{
    cli.execute_line("help go");
    EXPECT_EQ(out.str(), "go <x> <y> <z> <speed> — fly to x y z cm (forward, left, up) at speed\n");
}

TEST_F(ClassicCli, InfoSurvivesTimeouts)
{
    for (int i = 0; i < 3; ++i)
    {
        mock->reply(std::nullopt); // battery? times out, retries included
    }
    mock->reply("90");
    cli.execute_line("info");
    EXPECT_NE(out.str().find("drone:      Tello (SDK 1.3)"), std::string::npos);
    EXPECT_NE(out.str().find("battery:    unavailable"), std::string::npos);
    EXPECT_NE(out.str().find("wifi snr:   90"), std::string::npos);
    EXPECT_EQ(out.str().find("serial"), std::string::npos);
}

TEST_F(ClassicCli, StateWithoutTelemetry)
{
    cli.execute_line("state");
    EXPECT_EQ(out.str(), "no telemetry received yet\n");
}

TEST_F(ClassicCli, EmergencyWithoutConfirmation)
{
    cli.execute_line("takeoff; emergency");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "emergency"}));
    EXPECT_FALSE(cli.airborne());
}

TEST_F(ClassicCli, HoverNeedsSdk2)
{
    cli.execute_line("hover");
    EXPECT_NE(out.str().find("'hover' is not supported by Tello (SDK 1.3)"), std::string::npos);
    EXPECT_TRUE(mock->requests.empty());
}

TEST_F(EduCli, HoverAndStopSendStop)
{
    cli.execute_line("hover; stop");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"stop", "stop"}));
}

TEST_F(ClassicCli, AirborneFollowsTakeoffAndLand)
{
    EXPECT_FALSE(cli.airborne());
    mock->reply("error Battery low");
    cli.execute_line("takeoff");
    EXPECT_FALSE(cli.airborne()); // failed takeoff
    cli.execute_line("takeoff");
    EXPECT_TRUE(cli.airborne());
    cli.execute_line("land");
    EXPECT_FALSE(cli.airborne());
}

TEST_F(ClassicCli, InterruptOnGroundDoesNothing)
{
    cli.on_interrupt();
    EXPECT_TRUE(mock->requests.empty());
    EXPECT_EQ(out.str(), "");
}

TEST_F(ClassicCli, InterruptInFlightLands)
{
    cli.execute_line("takeoff");
    cli.on_interrupt();
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "land"}));
    EXPECT_FALSE(cli.airborne());
    EXPECT_NE(out.str().find("Ctrl+C in flight: landing"), std::string::npos);
}

TEST_F(ClassicCli, InterruptSkipsRestOfChain)
{
    cli.execute_line("takeoff");
    interrupted = true;
    EXPECT_TRUE(cli.execute_line("forward 100; forward 100"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff"}));
    EXPECT_NE(out.str().find("interrupted"), std::string::npos);
}

TEST_F(ClassicCli, MissionRunsLinesSkipsComments)
{
    std::istringstream plan("# demo\ntakeoff\n\nup 50 # higher\n  cw 90; ccw 90\nland\n");
    EXPECT_TRUE(cli.run_mission(plan, "demo"));
    EXPECT_EQ(mock->requests,
              (std::vector<std::string>{"takeoff", "up 50", "cw 90", "ccw 90", "land"}));
    EXPECT_NE(out.str().find("demo:4> up 50"), std::string::npos);
}

TEST_F(ClassicCli, MissionStopsAndLandsOnError)
{
    std::istringstream plan("takeoff\nforward 600\nforward 100\n");
    EXPECT_FALSE(cli.run_mission(plan, "bad"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "land"}));
    EXPECT_NE(out.str().find("bad: stopped at line 2"), std::string::npos);
    EXPECT_FALSE(cli.airborne());
}

TEST_F(ClassicCli, MissionErrorOnGroundDoesNotLand)
{
    std::istringstream plan("selfie\ntakeoff\n");
    EXPECT_FALSE(cli.run_mission(plan, "m"));
    EXPECT_TRUE(mock->requests.empty());
}

TEST_F(ClassicCli, MissionExitEndsMissionNotProgram)
{
    std::istringstream plan("battery\nexit\ntakeoff\n");
    mock->reply("80");
    EXPECT_TRUE(cli.run_mission(plan, "m"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"battery?"}));
    EXPECT_TRUE(cli.execute_line("land")); // the shell still runs
}

TEST_F(ClassicCli, MissionInterruptedLeavesLandingToCaller)
{
    std::istringstream plan("forward 100\nforward 100\n");
    cli.execute_line("takeoff");
    interrupted = true; // Ctrl+C already pressed: nothing more is sent
    EXPECT_FALSE(cli.run_mission(plan, "m"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff"}));
    EXPECT_TRUE(cli.airborne()); // main() calls on_interrupt() to land
}

TEST_F(ClassicCli, SquareFliesFourSides)
{
    cli.execute_line("takeoff; square 100; land");
    EXPECT_EQ(mock->requests,
              (std::vector<std::string>{"takeoff", "forward 100", "cw 90", "forward 100", "cw 90",
                                        "forward 100", "cw 90", "forward 100", "cw 90", "land"}));
}

TEST_F(ClassicCli, MissionFileMissing)
{
    cli.execute_line("mission /nonexistent/plan.txt");
    EXPECT_NE(out.str().find("cannot open"), std::string::npos);
}

TEST_F(ClassicCli, SquareInsideMission)
{
    std::istringstream plan("takeoff\nsquare 50\nland\n");
    EXPECT_TRUE(cli.run_mission(plan, "m"));
    EXPECT_EQ(mock->requests.size(), 10u);
}

TEST_F(ClassicCli, SquareFailureInShellLands)
{
    mock->reply("ok");               // takeoff
    mock->reply("ok");               // forward
    mock->reply("error Motor stop"); // cw 90
    cli.execute_line("takeoff; square 100");
    EXPECT_EQ(mock->requests,
              (std::vector<std::string>{"takeoff", "forward 100", "cw 90", "land"}));
}

TEST_F(ClassicCli, MissionsDoNotNest)
{
    const auto path = std::filesystem::temp_directory_path() / "tello_nested_mission.txt";
    std::ofstream(path) << "battery\n";
    std::istringstream plan("takeoff\nmission " + path.string() + "\n");
    EXPECT_FALSE(cli.run_mission(plan, "outer"));
    std::filesystem::remove(path);
    EXPECT_NE(out.str().find("missions do not nest"), std::string::npos);
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "land"}));
}

// 'land' answered ok while telemetry may say otherwise: a late reply to an earlier command.
class LandTelemetryCli : public ::testing::Test
{
protected:
    // Sends "h:<height>;" telemetry to the listener every 20 ms until the test ends.
    void send_height(int height)
    {
        sender = std::jthread(
            [this, height](const std::stop_token& stop)
            {
                const tello::UdpSocket socket("127.0.0.1", 0);
                while (!stop.stop_requested())
                {
                    socket.send_to("h:" + std::to_string(height) + ";", "127.0.0.1",
                                   listener.local_port());
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            });
    }

    std::shared_ptr<MockTransport> mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone{mock};
    std::ostringstream out;
    tello::StateListener listener{"127.0.0.1", 0};
    cli::CommandProcessor cli{drone,
                              out,
                              {"127.0.0.1", 8889, 8890, {}, {}},
                              &listener,
                              [](std::chrono::milliseconds)
                              {
                              }};
    std::jthread sender; // last: stops before the listener goes away
};

TEST_F(LandTelemetryCli, LandConfirmedWhenTelemetryShowsTheGround)
{
    send_height(0);
    EXPECT_TRUE(cli.execute_line("takeoff; land"));
    EXPECT_EQ(out.str().find("error"), std::string::npos) << out.str();
    EXPECT_TRUE(cli.land_before_exit());
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "land"}));
}

TEST_F(LandTelemetryCli, OkWithTheDroneStillUpIsNotALanding)
{
    send_height(120);
    EXPECT_TRUE(cli.execute_line("takeoff; land"));
    EXPECT_NE(out.str().find("telemetry shows h=120 cm: it may still be flying"), std::string::npos)
        << out.str();
    send_height(0);
    EXPECT_TRUE(cli.land_before_exit()); // still in the air: lands again
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "land", "land"}));
}

TEST_F(LandTelemetryCli, WithoutFreshTelemetryTheReplyStands)
{
    EXPECT_TRUE(cli.execute_line("takeoff; land"));
    EXPECT_EQ(out.str().find("error"), std::string::npos) << out.str();
    EXPECT_TRUE(cli.land_before_exit());
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "land"}));
}

} // namespace
