// Safe exit and emergency landing (TEL-00057): land_now, landing before exit, hang watchdog,
// and the real 'tello' program killed in the air against the simulator.

#include "cli/command_processor.hpp"
#include "drone/flight_guard.hpp"
#include "mock_transport.hpp"
#include "simulator.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_socket.hpp"
#include "tello/udp_transport.hpp"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

using std::chrono::milliseconds;
using tello::test::MockTransport;

std::size_t count(const std::vector<std::string>& v, const std::string& s)
{
    return static_cast<std::size_t>(std::count(v.begin(), v.end(), s));
}

TEST(LandNowTest, StopsThenSendsLandThreeTimes)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.land_now();
    EXPECT_EQ(mock->sent_now, (std::vector<std::string>{"rc 0 0 0 0", "land", "land", "land"}));
    EXPECT_TRUE(mock->requests.empty());
}

TEST(LandNowTest, KeepaliveIsNotACommand)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.connect();
    std::this_thread::sleep_for(milliseconds(60));
    drone.keepalive();
    EXPECT_LT(drone.idle_for(), milliseconds(50));
    EXPECT_GE(drone.command_idle_for(), milliseconds(50));
}

class SafeExitCli : public ::testing::Test
{
protected:
    std::shared_ptr<MockTransport> mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone{mock};
    std::ostringstream out;
    std::chrono::steady_clock::duration stalled_in_pause{1};
    cli::CommandProcessor cli{drone,
                              out,
                              {"127.0.0.1", 8889, 8890, {}, {}},
                              nullptr,
                              [this](milliseconds)
                              {
                                  stalled_in_pause = cli.stalled_for();
                              }};
};

TEST(UrgentCommandsTest, KeepaliveWaitsOnlyBriefly)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.keepalive();
    EXPECT_EQ(mock->last_timeout, milliseconds(1'000));
    drone.land();
    EXPECT_EQ(mock->last_timeout, milliseconds(20'000));
}

TEST(UrgentCommandsTest, EmergencyGoesOutAtOnceThenAsksForTheReply)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.emergency();
    EXPECT_EQ(mock->sent_now, (std::vector<std::string>{"emergency"}));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"emergency"}));
}

TEST(UrgentCommandsTest, EmergencyIsNotHeldBehindAPendingRequest)
{
    tello::UdpSocket peer("127.0.0.1", 0); // a drone that never answers
    auto transport = std::make_shared<tello::UdpTransport>("127.0.0.1", peer.local_port());
    tello::DroneTimeouts timeouts;
    timeouts.action = milliseconds(200);
    tello::TelloClassic drone(transport, timeouts);
    std::thread keepalive(
        [&drone]
        {
            EXPECT_THROW(drone.keepalive(), drone::NoReply);
        });
    const auto first = peer.receive(milliseconds(2'000));
    ASSERT_TRUE(first);
    EXPECT_EQ(first->data, "command");
    const auto start = std::chrono::steady_clock::now();
    std::thread panic(
        [&drone]
        {
            EXPECT_THROW(drone.emergency(), drone::NoReply);
        });
    const auto second = peer.receive(milliseconds(2'000));
    ASSERT_TRUE(second);
    EXPECT_EQ(second->data, "emergency");
    EXPECT_LT(std::chrono::steady_clock::now() - start, milliseconds(500)); // keepalive waits 1 s
    keepalive.join();
    panic.join();
}

TEST_F(SafeExitCli, OnTheGroundNothingToLand)
{
    EXPECT_TRUE(cli.land_before_exit());
    EXPECT_EQ(count(mock->requests, "land"), 0u);
}

TEST_F(SafeExitCli, ExitInTheAirLandsFirst)
{
    cli.execute_line("takeoff");
    EXPECT_FALSE(cli.execute_line("exit"));
    EXPECT_TRUE(cli.land_before_exit());
    EXPECT_EQ(count(mock->requests, "land"), 1u);
    EXPECT_FALSE(cli.airborne());
    EXPECT_NE(out.str().find("landing before exit"), std::string::npos);
}

TEST_F(SafeExitCli, FailedLandingKeepsTheShellThenExitAgainLeaves)
{
    cli.execute_line("takeoff");
    EXPECT_FALSE(cli.execute_line("exit"));
    mock->reply("error Motor stop");
    EXPECT_FALSE(cli.land_before_exit());
    EXPECT_NE(out.str().find("landing failed"), std::string::npos);
    EXPECT_TRUE(cli.execute_line("battery")); // the shell goes on
    EXPECT_FALSE(cli.execute_line("exit"));
    EXPECT_TRUE(cli.land_before_exit()); // another try
    EXPECT_EQ(count(mock->requests, "land"), 2u);
}

TEST_F(SafeExitCli, ExitRightAfterFailedLandingLeavesAnyway)
{
    cli.execute_line("takeoff");
    cli.execute_line("exit");
    mock->reply("error Motor stop");
    EXPECT_FALSE(cli.land_before_exit());
    EXPECT_FALSE(cli.execute_line("exit"));
    EXPECT_TRUE(cli.land_before_exit());
    EXPECT_EQ(count(mock->requests, "land"), 1u);
    EXPECT_TRUE(cli.airborne());
}

TEST_F(SafeExitCli, LostTakeoffReplyCountsAsInTheAir)
{
    mock->reply(std::nullopt);
    cli.execute_line("takeoff");
    EXPECT_TRUE(cli.airborne());
    EXPECT_NE(out.str().find("may be flying"), std::string::npos);
    EXPECT_TRUE(cli.land_before_exit());
    EXPECT_EQ(count(mock->requests, "land"), 1u);
    EXPECT_FALSE(cli.airborne());
}

TEST_F(SafeExitCli, RefusedTakeoffStaysOnTheGround)
{
    mock->reply("error No valid imu");
    cli.execute_line("takeoff");
    EXPECT_FALSE(cli.airborne());
    EXPECT_TRUE(cli.land_before_exit());
    EXPECT_EQ(count(mock->requests, "land"), 0u);
}

TEST_F(SafeExitCli, InTheAirWhileTakeoffIsInFlight)
{
    bool during = false;
    mock->on_request = [&](std::string_view command)
    {
        if (command == "takeoff")
        {
            during = cli.airborne();
        }
    };
    cli.execute_line("takeoff");
    EXPECT_TRUE(during);
}

TEST_F(SafeExitCli, StalledOnlyWhileACommandRunsInTheAir)
{
    EXPECT_EQ(cli.stalled_for(), std::chrono::steady_clock::duration::zero());
    std::chrono::steady_clock::duration during{};
    mock->on_request = [&](std::string_view command)
    {
        if (command == "up 50")
        {
            std::this_thread::sleep_for(milliseconds(40));
            during = cli.stalled_for();
        }
    };
    cli.execute_line("takeoff; up 50");
    EXPECT_GE(during, milliseconds(30));
    EXPECT_LT(during, milliseconds(1000));
    EXPECT_EQ(cli.stalled_for(), std::chrono::steady_clock::duration::zero()); // at the prompt
    cli.execute_line("wait 1");
    EXPECT_EQ(stalled_in_pause, std::chrono::steady_clock::duration::zero()); // planned pause
}

// --- Hang watchdog in FlightGuard. ---

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
    bool contains(const std::string& text)
    {
        const std::lock_guard lock(mutex);
        return std::any_of(list.begin(), list.end(),
                           [&](const std::string& m)
                           {
                               return m.find(text) != std::string::npos;
                           });
    }
};

drone::GuardSettings fast_watchdog()
{
    drone::GuardSettings s;
    s.keepalive_after = milliseconds(30);
    s.check_period = milliseconds(10);
    s.stuck_after = milliseconds(100);
    return s;
}

TEST(HangWatchdogTest, StuckProgramLandsOnceAndStopsKeepalive)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.connect();
    std::atomic<long long> stalled_ms{500};
    Messages messages;
    {
        drone::FlightGuard guard(drone, fast_watchdog(), {}, messages.sink(),
                                 [&stalled_ms]
                                 {
                                     return std::chrono::steady_clock::duration(
                                         milliseconds(stalled_ms.load()));
                                 });
        std::this_thread::sleep_for(milliseconds(300));
    }
    EXPECT_EQ(count(mock->requests, "land"), 1u);
    EXPECT_EQ(count(mock->requests, "command"), 1u); // connect only: no keepalive while stuck
    EXPECT_TRUE(messages.contains("program stuck"));
}

TEST(HangWatchdogTest, ShortStallIsNotAHang)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.connect();
    {
        drone::FlightGuard guard(drone, fast_watchdog(), {}, {},
                                 []
                                 {
                                     return std::chrono::steady_clock::duration(milliseconds(50));
                                 });
        std::this_thread::sleep_for(milliseconds(200));
    }
    EXPECT_EQ(count(mock->requests, "land"), 0u);
    EXPECT_GE(count(mock->requests, "command"), 2u); // keepalive goes on
}

TEST(HangWatchdogTest, ProgramBackReArmsKeepalive)
{
    auto mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone(mock);
    drone.connect();
    std::atomic<long long> stalled_ms{500};
    Messages messages;
    {
        drone::FlightGuard guard(drone, fast_watchdog(), {}, messages.sink(),
                                 [&stalled_ms]
                                 {
                                     return std::chrono::steady_clock::duration(
                                         milliseconds(stalled_ms.load()));
                                 });
        std::this_thread::sleep_for(milliseconds(100));
        stalled_ms.store(0);
        std::this_thread::sleep_for(milliseconds(200));
    }
    EXPECT_TRUE(messages.contains("program responds again"));
    EXPECT_GE(count(mock->requests, "command"), 2u);
}

// --- The real program against the simulator. ---

class TelloProcess : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tello::sim::SimConfig config;
        config.command_port = 0;
        config.state_port = 0;
        config.video_port = 0;
        sim = std::make_unique<tello::sim::Simulator>(config);
        home = std::filesystem::temp_directory_path() /
               ("tello_safe_exit_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(home);
        std::filesystem::create_directories(home);
    }
    void TearDown() override
    {
        if (pid > 0)
        {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
        }
        if (input >= 0)
        {
            ::close(input);
        }
        std::filesystem::remove_all(home);
    }

    // Starts 'tello' with piped stdin against the simulator, output and log under home.
    void start()
    {
        std::array<int, 2> fds{};
        ASSERT_EQ(::pipe(fds.data()), 0);
        const std::string port = std::to_string(sim->command_port());
        const std::string log_dir = (home / "logs").string();
        const std::string out_file = (home / "out.txt").string();
        // Everything the child needs is built before fork(): after it only async-signal-safe calls.
        std::vector<std::string> env{"HOME=" + home.string()};
        for (char** e = environ; *e != nullptr; ++e)
        {
            const std::string_view var(*e);
            if (!var.starts_with("HOME=") && !var.starts_with("XDG_"))
            {
                env.emplace_back(var);
            }
        }
        std::vector<char*> envp;
        for (auto& var : env)
        {
            envp.push_back(var.data());
        }
        envp.push_back(nullptr);
        std::vector<std::string> args{TELLO_CLI_PATH, "--host", "127.0.0.1", "--port", port,
                                      "--state-port", "0",      "--log-dir", log_dir};
        std::vector<char*> argv;
        for (auto& arg : args)
        {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        pid = ::fork();
        ASSERT_GE(pid, 0);
        if (pid == 0)
        {
            ::dup2(fds[0], STDIN_FILENO);
            ::close(fds[0]);
            ::close(fds[1]);
            const int out = ::open(out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            ::dup2(out, STDOUT_FILENO);
            ::dup2(out, STDERR_FILENO);
            ::execve(TELLO_CLI_PATH, argv.data(), envp.data());
            ::_exit(127);
        }
        ::close(fds[0]);
        input = fds[1];
    }

    void say(const std::string& text) const
    {
        ASSERT_EQ(::write(input, text.data(), text.size()), static_cast<ssize_t>(text.size()));
    }

    template <typename Pred> bool wait_for(Pred pred, milliseconds limit = milliseconds(5000))
    {
        const auto until = std::chrono::steady_clock::now() + limit;
        while (!pred() && std::chrono::steady_clock::now() < until)
        {
            std::this_thread::sleep_for(milliseconds(10));
        }
        return pred();
    }

    int wait_exit()
    {
        int status = 0;
        ::waitpid(pid, &status, 0);
        pid = -1;
        return status;
    }

    std::string log_text() const
    {
        std::string all;
        for (const auto& entry : std::filesystem::directory_iterator(home / "logs"))
        {
            std::ifstream f(entry.path());
            all += std::string(std::istreambuf_iterator<char>(f), {});
        }
        return all;
    }

    std::unique_ptr<tello::sim::Simulator> sim;
    std::filesystem::path home;
    pid_t pid = -1;
    int input = -1;
};

TEST_F(TelloProcess, KilledInTheAirLands)
{
    for (const int sig : {SIGTERM, SIGHUP})
    {
        start();
        say("takeoff\n");
        ASSERT_TRUE(wait_for(
            [this]
            {
                return sim->model().flying;
            }));
        ::kill(pid, sig);
        const int status = wait_exit();
        EXPECT_TRUE(WIFSIGNALED(status));
        EXPECT_TRUE(wait_for(
            [this]
            {
                return !sim->model().flying;
            },
            milliseconds(2000)))
            << "signal " << sig;
        EXPECT_NE(log_text().find("EMERGENCY"), std::string::npos);
        ::close(input);
        input = -1;
    }
}

TEST_F(TelloProcess, ExitInTheAirLandsFirst)
{
    start();
    say("takeoff\nexit\n");
    const int status = wait_exit();
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    EXPECT_FALSE(sim->model().flying);
    EXPECT_NE(log_text().find("landing first"), std::string::npos);
}

TEST_F(TelloProcess, EndOfInputInTheAirLandsFirst)
{
    start();
    say("takeoff\n");
    ::close(input);
    input = -1;
    const int status = wait_exit();
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    EXPECT_FALSE(sim->model().flying);
}

} // namespace
