// flower / wheel / loop: takeoff, a circle with photos, back to the center, land.

#include "cli/command_processor.hpp"
#include "drone/dead_reckoning.hpp"
#include "mock_transport.hpp"
#include "simulator.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_socket.hpp"
#include "tello/udp_transport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{

using std::chrono::milliseconds;

class PhotoFigures : public ::testing::Test
{
protected:
    PhotoFigures()
        : dir(std::filesystem::temp_directory_path() /
              ("tello_figures_test_" + std::to_string(::getpid())))
    {
        std::uint16_t video_port = 0;
        {
            const tello::UdpSocket probe("127.0.0.1", 0);
            video_port = probe.local_port();
        }
        config.command_port = 0;
        config.state_port = 0;
        config.video_port = video_port;
        config.on_command = [this](std::string_view command, const std::optional<std::string>&)
        {
            const std::scoped_lock lock(mutex);
            log.emplace_back(command);
        };
    }
    ~PhotoFigures() override
    {
        cli.reset();
        drone.reset();
        sim.reset();
        std::filesystem::remove_all(dir);
    }

    void start()
    {
        sim = std::make_unique<tello::sim::Simulator>(config);
        drone = std::make_unique<tello::TelloClassic>(
            std::make_shared<tello::UdpTransport>("127.0.0.1", sim->command_port()),
            tello::DroneTimeouts{milliseconds(1000), milliseconds(500)});
        drone->set_video_port(config.video_port);
        drone->connect();
        cli = std::make_unique<cli::CommandProcessor>(
            *drone, out, cli::SessionInfo{"127.0.0.1", sim->command_port(), 0, dir, dir}, nullptr,
            [](milliseconds)
            {
            });
        const std::scoped_lock lock(mutex);
        log.clear();
    }

    std::vector<std::string> commands()
    {
        const std::scoped_lock lock(mutex);
        return log;
    }

    // Photos of the only figure flown, sorted by name.
    std::vector<std::string> photos() const
    {
        std::vector<std::string> names;
        for (const auto& folder : std::filesystem::directory_iterator(dir))
        {
            for (const auto& file : std::filesystem::directory_iterator(folder.path()))
            {
                if (file.file_size() > 0)
                {
                    names.push_back(folder.path().filename().string() + "/" +
                                    file.path().filename().string());
                }
            }
        }
        std::ranges::sort(names);
        return names;
    }

    // Lowest and highest height the go/takeoff commands reached (cm).
    std::pair<int, int> height_range()
    {
        int z = 0;
        int low = 1'000'000;
        int high = 0;
        for (const auto& c : commands())
        {
            if (c == "takeoff")
            {
                z = 80;
            }
            else if (c.starts_with("go "))
            {
                std::istringstream in(c.substr(3));
                int x = 0;
                int y = 0;
                int up = 0;
                in >> x >> y >> up;
                z += up;
            }
            else
            {
                continue;
            }
            low = std::min(low, z);
            high = std::max(high, z);
        }
        return {low, high};
    }

    std::filesystem::path dir;
    tello::sim::SimConfig config;
    std::mutex mutex;
    std::vector<std::string> log;
    std::ostringstream out;
    std::unique_ptr<tello::sim::Simulator> sim;
    std::unique_ptr<tello::TelloClassic> drone;
    std::unique_ptr<cli::CommandProcessor> cli;
};

TEST_F(PhotoFigures, FlowerDefaultsCircleTheTakeoffPointCameraIn)
{
    start();
    ASSERT_TRUE(cli->execute_line("flower")) << out.str();
    const auto c = commands();
    ASSERT_GE(c.size(), 6U);
    EXPECT_EQ(c[0], "streamon");
    EXPECT_EQ(c[1], "takeoff");
    EXPECT_EQ(c[2], "go 0 0 120 30"); // up to 200 cm
    EXPECT_EQ(c[3], "go 100 0 0 30"); // out to the circle
    EXPECT_EQ(c[4], "cw 180");        // camera to the center
    EXPECT_EQ(c[5], "go 13 50 0 30"); // 30 degrees clockwise seen from above
    EXPECT_EQ(c[6], "cw 30");
    EXPECT_EQ(c.back(), "land");
    EXPECT_EQ(c[c.size() - 2], "go 87 -50 0 30"); // from the circle back to the center
    EXPECT_EQ(std::ranges::count(c, std::string("cw 30")), 11);

    const auto p = photos();
    ASSERT_EQ(p.size(), 12U);
    EXPECT_TRUE(p.front().starts_with("flower_")) << p.front();
    EXPECT_TRUE(p.front().ends_with("/01.jpg")) << p.front();
    EXPECT_TRUE(p.back().ends_with("/12.jpg")) << p.back();

    const auto m = sim->model();
    EXPECT_FALSE(m.flying);
    EXPECT_LT(std::hypot(m.x_cm, m.y_cm), 3.0);
    EXPECT_FALSE(cli->airborne());
    EXPECT_NE(out.str().find("flower: height 200 cm, radius 100 cm, 12 photos every 30 degrees, "
                             "camera in"),
              std::string::npos)
        << out.str();
    EXPECT_NE(out.str().find("flower: done, 12 of 12 photos"), std::string::npos) << out.str();
}

TEST_F(PhotoFigures, FlowerOutCounterClockwise)
{
    start();
    ASSERT_TRUE(cli->execute_line("flower 150 200 90 out ccw 50")) << out.str();
    const auto c = commands();
    EXPECT_EQ(c[2], "go 0 0 70 50");
    EXPECT_EQ(c[3], "go 200 0 0 50");
    EXPECT_EQ(c[4], "go -200 200 0 50"); // a quarter counter-clockwise, still facing out
    EXPECT_EQ(c[5], "ccw 90");
    EXPECT_EQ(photos().size(), 4U);
    EXPECT_LT(std::hypot(sim->model().x_cm, sim->model().y_cm), 3.0);
}

TEST_F(PhotoFigures, WheelStartsAtTheTopAndSwingsRightFacingThePilot)
{
    start();
    ASSERT_TRUE(cli->execute_line("wheel")) << out.str();
    const auto c = commands();
    EXPECT_EQ(c[2], "go 0 0 120 30");  // the center, 200 cm
    EXPECT_EQ(c[3], "go 0 0 100 30");  // the top
    EXPECT_EQ(c[4], "cw 180");         // camera to the pilot
    EXPECT_EQ(c[5], "go 0 50 -13 30"); // the pilot's right is the drone's left now
    EXPECT_EQ(std::ranges::count_if(c,
                                    [](const std::string& s)
                                    {
                                        return s.starts_with("cw") || s.starts_with("ccw");
                                    }),
              1);
    EXPECT_EQ(c.back(), "land");
    EXPECT_EQ(height_range(), (std::pair<int, int>{80, 300}));
    EXPECT_EQ(photos().size(), 12U);
    EXPECT_LT(std::hypot(sim->model().x_cm, sim->model().y_cm), 3.0);
    EXPECT_FALSE(sim->model().flying);
}

TEST_F(PhotoFigures, LoopTowardThePilotCameraSide)
{
    start();
    ASSERT_TRUE(cli->execute_line("loop 250 100 90 toward side")) << out.str();
    const auto c = commands();
    EXPECT_EQ(c[2], "go 0 0 170 30");
    EXPECT_EQ(c[3], "go 0 0 100 30");
    EXPECT_EQ(c[4], "cw 90");
    EXPECT_EQ(c[5], "go 0 -100 -100 30"); // towards the pilot: the drone's right now
    EXPECT_EQ(height_range(), (std::pair<int, int>{80, 350}));
    EXPECT_EQ(photos().size(), 4U);
    EXPECT_LT(std::hypot(sim->model().x_cm, sim->model().y_cm), 3.0);
}

TEST_F(PhotoFigures, FailedTurnLandsWhereTheDroneIs)
{
    config.fail_commands = {"cw"};
    start();
    EXPECT_TRUE(cli->execute_line("flower; battery"));
    const auto c = commands();
    EXPECT_EQ(c.back(), "land"); // battery never ran
    EXPECT_FALSE(sim->model().flying);
    EXPECT_FALSE(cli->airborne());
    EXPECT_NE(out.str().find("flower: landing"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("flower: stopped before the first photo"), std::string::npos)
        << out.str();
    EXPECT_TRUE(photos().empty());
}

class FigureRefusals : public ::testing::Test
{
protected:
    std::shared_ptr<tello::test::MockTransport> mock =
        std::make_shared<tello::test::MockTransport>();
    tello::TelloClassic drone{mock};
    std::ostringstream out;
    cli::CommandProcessor cli{drone, out, {"127.0.0.1", 8889, 0, {}, {}}};
};

TEST_F(FigureRefusals, BadArgumentsSendNothing)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"flower 200 100 7", "flower step (degrees) must be 10..120, got 7"},
        {"flower 200 100 25", "flower step must divide 360, got 25"},
        {"flower 200 50 10", "10 degrees of a 50 cm circle is a 9 cm leg"},
        {"flower 2000", "flower height (cm) must be 50..1000, got 2000"},
        {"flower 200 30", "flower radius (cm) must be 50..500, got 30"},
        {"flower 200 100 30 5", "flower speed must be 10..100, got 5"},
        {"flower 1 2 3 4 5", "usage: flower [height] [radius] [step] [in|out] [cw|ccw] [speed]"},
        {"flower 200 100 30 side", "usage: flower"},
        {"wheel 100 100",
         "wheel: the lowest point (height - radius) must be at least 50 cm, got 0"},
        {"wheel 900 200", "wheel: the highest point (height + radius) must be at most 1000 cm"},
        {"loop 200 100 30 cw", "usage: loop"},
    };
    for (const auto& [line, error] : cases)
    {
        out.str("");
        cli.execute_line(line);
        EXPECT_NE(out.str().find(error), std::string::npos) << line << ": " << out.str();
    }
    EXPECT_TRUE(mock->requests.empty());
}

TEST_F(FigureRefusals, StartsOnTheGroundOnly)
{
    cli.execute_line("takeoff");
    mock->requests.clear();
    cli.execute_line("wheel");
    EXPECT_NE(out.str().find("wheel: land first, the figure starts with a takeoff"),
              std::string::npos)
        << out.str();
    EXPECT_TRUE(mock->requests.empty());
}

} // namespace

namespace
{

class FigureDryRun : public ::testing::Test
{
protected:
    std::shared_ptr<tello::test::MockTransport> mock =
        std::make_shared<tello::test::MockTransport>();
    tello::TelloClassic drone{mock};
    std::ostringstream out;
    cli::CommandProcessor cli{drone, out, {"127.0.0.1", 8889, 0, {}, {}}};
};

TEST_F(FigureDryRun, PrintsTheFlightAndSendsNothing)
{
    cli.execute_line("flower dry");
    const std::string text = out.str();
    EXPECT_TRUE(mock->requests.empty());
    EXPECT_NE(text.find("flower (dry run, nothing is sent): height 200 cm, radius 100 cm, 12 "
                        "photos every 30 degrees, camera in\n"
                        "  takeoff\n"
                        "  go 0 0 120 30\n"
                        "  go 100 0 0 30\n"
                        "  cw 180\n"
                        "  photo 01\n"
                        "  go 13 50 0 30\n"
                        "  cw 30\n"
                        "  photo 02\n"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("  photo 12\n  go 13 50 0 30\n  go 87 -50 0 30\n  land\n"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("flower: 29 commands, 12 photos, about "), std::string::npos) << text;
    EXPECT_NE(text.find(" highest 200 cm, about "), std::string::npos) << text;
    EXPECT_NE(text.find(" % battery\n"), std::string::npos) << text;
}

TEST_F(FigureDryRun, WorksInTheAirAndChecksArguments)
{
    cli.execute_line("takeoff");
    mock->requests.clear();
    cli.execute_line("wheel 250 150 dry");
    EXPECT_TRUE(mock->requests.empty());
    EXPECT_NE(out.str().find("highest 400 cm"), std::string::npos) << out.str();
    out.str("");
    cli.execute_line("wheel 100 100 dry");
    EXPECT_NE(out.str().find("wheel: the lowest point"), std::string::npos) << out.str();
    EXPECT_EQ(out.str().find("dry run"), std::string::npos) << out.str();
}

TEST(FigureEstimate, CountsTimeDistanceAndBattery)
{
    cli::FigurePlan plan;
    plan.name = "test";
    plan.speed = 50;
    plan.steps = {cli::FigureStep::move(0, 0, 180), cli::FigureStep::move(500, 0, 180),
                  cli::FigureStep::wait(milliseconds(4000)), cli::FigureStep::move(0, 0, 180)};
    drone::DeadReckoning paper(80);
    const auto e = cli::estimate(plan, paper);
    EXPECT_EQ(e.lines, (std::vector<std::string>{"takeoff", "go 0 0 100 50", "go 500 0 0 50",
                                                 "wait 4000 ms", "go -500 0 0 50", "land"}));
    EXPECT_EQ(e.commands, 5);
    EXPECT_DOUBLE_EQ(e.highest_cm, 180.0);
    EXPECT_NEAR(e.distance_cm, 1100.0, 1e-6); // takeoff and land are not counted
    // 6 takeoff + 5 land + 3 x (1 + flight) + 4 wait = 18 + 1100 / 50 = 40 s.
    EXPECT_EQ(e.duration.count(), 40);
    EXPECT_EQ(e.battery_percent, 6); // 40 s of 13 min
}

} // namespace
