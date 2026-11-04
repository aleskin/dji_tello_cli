// flower / wheel / loop: takeoff, a circle with photos, back to the center, land.

#include "cli/command_processor.hpp"
#include "drone/dead_reckoning.hpp"
#include "mock_transport.hpp"
#include "simulator.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_socket.hpp"
#include "tello/udp_transport.hpp"

extern "C"
{
#include <libavformat/avformat.h>
}

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
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

    // real_sleep: photo settles take real time, so a recording gets its keyframes.
    void start(bool real_sleep = false)
    {
        sim = std::make_unique<tello::sim::Simulator>(config);
        drone = std::make_unique<tello::TelloClassic>(
            std::make_shared<tello::UdpTransport>("127.0.0.1", sim->command_port()),
            tello::DroneTimeouts{milliseconds(1000), milliseconds(500)});
        drone->set_video_port(config.video_port);
        drone->connect();
        cli = std::make_unique<cli::CommandProcessor>(
            *drone, out, cli::SessionInfo{"127.0.0.1", sim->command_port(), 0, dir, dir}, nullptr,
            [real_sleep](milliseconds pause)
            {
                if (real_sleep)
                {
                    std::this_thread::sleep_for(pause);
                }
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
            if (!folder.is_directory())
            {
                continue; // a recording
            }
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

TEST_F(PhotoFigures, PanoramaTurnsOnTheSpot)
{
    start();
    ASSERT_TRUE(cli->execute_line("panorama 150 90 ccw")) << out.str();
    EXPECT_EQ(commands(), (std::vector<std::string>{"streamon", "takeoff", "go 0 0 70 30", "ccw 90",
                                                    "ccw 90", "ccw 90", "land"}));
    EXPECT_EQ(photos().size(), 4U);
    EXPECT_NE(out.str().find("panorama: height 150 cm, 4 photos every 90 degrees ccw"),
              std::string::npos)
        << out.str();
}

TEST_F(PhotoFigures, TowerClimbsWithPhotos)
{
    start();
    ASSERT_TRUE(cli->execute_line("tower 100 300 100")) << out.str();
    EXPECT_EQ(commands(), (std::vector<std::string>{"streamon", "takeoff", "go 0 0 20 30",
                                                    "go 0 0 100 30", "go 0 0 100 30", "land"}));
    EXPECT_EQ(photos().size(), 3U);
    EXPECT_NE(out.str().find("tower: 3 photos from 100 to 300 cm every 100 cm"), std::string::npos)
        << out.str();
}

TEST_F(PhotoFigures, DronieFliesAwayFilmingThePilot)
{
    start(true);
    ASSERT_TRUE(cli->execute_line("dronie 400 250 50")) << out.str();
    EXPECT_EQ(commands(), (std::vector<std::string>{"streamon", "takeoff", "cw 180",
                                                    "go -400 0 170 50", "go 400 0 0 50", "land"}));
    EXPECT_TRUE(photos().empty());
    EXPECT_NE(out.str().find("dronie: camera on you, away 400 cm and up to 250 cm, video"),
              std::string::npos)
        << out.str();
    EXPECT_NE(out.str().find("dronie: saved "), std::string::npos) << out.str();
}

TEST_F(PhotoFigures, RevealStartsLowAndRisesForward)
{
    start();
    ASSERT_TRUE(cli->execute_line("reveal")) << out.str();
    EXPECT_EQ(commands(), (std::vector<std::string>{"streamon", "takeoff", "go 0 0 -30 30",
                                                    "go 300 0 150 30", "go -300 0 0 30", "land"}));
    EXPECT_NE(out.str().find("reveal: camera forward, from 50 cm forward 300 cm and up to 200 "
                             "cm, video"),
              std::string::npos)
        << out.str();
}

TEST_F(PhotoFigures, BounceWithATurn)
{
    start();
    ASSERT_TRUE(cli->execute_line("bounce 2 50 ccw")) << out.str();
    EXPECT_EQ(commands(),
              (std::vector<std::string>{"takeoff", "go 0 0 20 30", "go 0 0 50 30", "ccw 90",
                                        "go 0 0 -50 30", "ccw 90", "go 0 0 50 30", "ccw 90",
                                        "go 0 0 -50 30", "ccw 90", "land"}));
    EXPECT_NE(out.str().find("bounce: 2 times between 100 and 150 cm, one turn ccw"),
              std::string::npos)
        << out.str();
}

TEST_F(PhotoFigures, FlipsShow)
{
    start();
    ASSERT_TRUE(cli->execute_line("flips f b f")) << out.str();
    EXPECT_EQ(commands(), (std::vector<std::string>{"battery?", "takeoff", "go 0 0 70 30", "flip f",
                                                    "flip b", "flip f", "land"}));
    EXPECT_NE(out.str().find("flips: 3 flips (f b f) at 150 cm"), std::string::npos) << out.str();
}

TEST_F(PhotoFigures, FlipsNeedHalfTheBattery)
{
    config.battery = 40;
    start();
    cli->execute_line("flips");
    EXPECT_EQ(commands(), (std::vector<std::string>{"battery?"}));
    EXPECT_NE(out.str().find("flips: battery 40 %, needs 50 %"), std::string::npos) << out.str();
}

TEST_F(PhotoFigures, HelixClimbsAndWidens)
{
    start();
    ASSERT_TRUE(cli->execute_line("helix 1 100 200 100 200 90 ccw")) << out.str();
    const auto c = commands();
    EXPECT_EQ(c[0], "streamon");
    EXPECT_EQ(c[2], "go 0 0 20 30");  // 100 cm
    EXPECT_EQ(c[3], "go 100 0 0 30"); // out on the first radius
    EXPECT_EQ(c[4], "cw 180");        // camera in
    EXPECT_EQ(std::ranges::count(c, std::string("ccw 90")), 4);
    EXPECT_EQ(c.back(), "land");
    EXPECT_EQ(height_range(), (std::pair<int, int>{80, 200}));
    EXPECT_LT(std::hypot(sim->model().x_cm, sim->model().y_cm), 3.0);
    EXPECT_NE(out.str().find("helix: 1 turns ccw, height 100 to 200 cm, radius 100 to 200 cm, "
                             "camera in, video"),
              std::string::npos)
        << out.str();
}

TEST_F(PhotoFigures, BoomerangRisesAtTheFarEnd)
{
    start();
    ASSERT_TRUE(cli->execute_line("boomerang")) << out.str();
    const auto c = commands();
    EXPECT_EQ(c[2], "go 0 0 20 30");   // 100 cm
    EXPECT_EQ(c[3], "go -200 0 0 30"); // the near end, behind the takeoff point
    EXPECT_EQ(c.back(), "land");
    EXPECT_EQ(height_range(), (std::pair<int, int>{80, 200}));
    EXPECT_LT(std::hypot(sim->model().x_cm, sim->model().y_cm), 3.0);
    EXPECT_NE(out.str().find("boomerang: oval 400 x 200 cm cw, from 100 up to 200 cm at the far "
                             "end, camera in, video"),
              std::string::npos)
        << out.str();
}

TEST_F(PhotoFigures, EightFlatAndUpright)
{
    start();
    ASSERT_TRUE(cli->execute_line("eight 100 200 90")) << out.str();
    EXPECT_EQ(commands(),
              (std::vector<std::string>{"streamon", "takeoff", "go 0 0 120 30", "go 100 -100 0 30",
                                        "go -100 -100 0 30", "go -100 100 0 30", "go 100 100 0 30",
                                        "go 100 100 0 30", "go -100 100 0 30", "go -100 -100 0 30",
                                        "go 100 -100 0 30", "land"}));
    out.str("");
    ASSERT_TRUE(cli->execute_line("eight 100 300 90 upright")) << out.str();
    EXPECT_EQ(height_range(), (std::pair<int, int>{80, 500}));
    EXPECT_LT(std::hypot(sim->model().x_cm, sim->model().y_cm), 3.0);
}

TEST_F(PhotoFigures, PolygonAndStar)
{
    start();
    ASSERT_TRUE(cli->execute_line("polygon 4 100 200")) << out.str();
    EXPECT_EQ(commands(),
              (std::vector<std::string>{"takeoff", "go 0 0 120 30", "go 100 0 0 30",
                                        "go -100 -100 0 30", "go -100 100 0 30", "go 100 100 0 30",
                                        "go 100 -100 0 30", "go -100 0 0 30", "land"}));
    ASSERT_TRUE(cli->execute_line("star 100 250 upright")) << out.str();
    EXPECT_EQ(height_range(), (std::pair<int, int>{80, 350}));
    EXPECT_LT(std::hypot(sim->model().x_cm, sim->model().y_cm), 3.0);
    EXPECT_NE(
        out.str().find("star: upright, 5 corners on a 100 cm circle 250 cm above the takeoff"),
        std::string::npos)
        << out.str();
}

TEST_F(PhotoFigures, RingsAtSeveralHeights)
{
    start();
    ASSERT_TRUE(cli->execute_line("rings 100 100 200 2 90")) << out.str();
    const auto c = commands();
    EXPECT_EQ(c[2], "go 0 0 20 30");
    EXPECT_EQ(c[3], "go 100 0 0 30");
    EXPECT_EQ(c[4], "cw 180");
    EXPECT_NE(std::ranges::find(c, std::string("go 0 0 100 30")), c.end()); // to the next ring
    EXPECT_EQ(c.back(), "land");
    EXPECT_EQ(height_range(), (std::pair<int, int>{80, 200}));
    EXPECT_EQ(photos().size(), 8U);
    EXPECT_LT(std::hypot(sim->model().x_cm, sim->model().y_cm), 3.0);
    EXPECT_NE(out.str().find("rings: 2 rings of 100 cm from 100 to 200 cm, 8 photos, camera in"),
              std::string::npos)
        << out.str();
}

TEST_F(PhotoFigures, TimelapseMakesAMovie)
{
    start();
    ASSERT_TRUE(cli->execute_line("timelapse 2 5")) << out.str();
    EXPECT_EQ(commands(),
              (std::vector<std::string>{"streamon", "takeoff", "go 0 0 120 30", "land"}));
    const auto p = photos();
    ASSERT_EQ(p.size(), 6U); // 5 frames and the movie
    EXPECT_TRUE(p.back().ends_with("/timelapse.mp4")) << p.back();
    EXPECT_NE(out.str().find("timelapse: movie "), std::string::npos) << out.str();

    AVFormatContext* movie = nullptr;
    const auto file = dir / p.back();
    ASSERT_EQ(avformat_open_input(&movie, file.c_str(), nullptr, nullptr), 0);
    ASSERT_GE(avformat_find_stream_info(movie, nullptr), 0);
    EXPECT_EQ(movie->streams[0]->nb_frames, 5);
    EXPECT_EQ(movie->streams[0]->codecpar->width, 960);
    avformat_close_input(&movie);
}

TEST_F(PhotoFigures, HyperlapseFliesForwardWithFrames)
{
    start();
    ASSERT_TRUE(cli->execute_line("hyperlapse 200 3")) << out.str();
    EXPECT_EQ(commands(),
              (std::vector<std::string>{"streamon", "takeoff", "go 0 0 120 20", "go 100 0 0 20",
                                        "go 100 0 0 20", "go -200 0 0 20", "land"}));
    EXPECT_EQ(photos().size(), 4U);
    EXPECT_NE(out.str().find("hyperlapse: movie "), std::string::npos) << out.str();
}

TEST_F(PhotoFigures, ScanSnakesAcrossTheWall)
{
    start();
    ASSERT_TRUE(cli->execute_line("scan 100 100 150 50")) << out.str();
    EXPECT_EQ(commands(),
              (std::vector<std::string>{"streamon", "takeoff", "go 0 50 20 30", "go 0 -50 0 30",
                                        "go 0 -50 0 30", "go 0 0 50 30", "go 0 50 0 30",
                                        "go 0 50 0 30", "go 0 -50 0 30", "land"}));
    EXPECT_EQ(photos().size(), 6U);
    EXPECT_EQ(height_range(), (std::pair{80, 150}));
}

TEST_F(PhotoFigures, RecordedFlightReplaysAsAMission)
{
    start();
    std::filesystem::create_directories(dir);
    const auto file = dir / "route.txt";
    ASSERT_TRUE(cli->execute_line("record start " + file.string()));
    ASSERT_TRUE(cli->execute_line("takeoff; forward 50; position; cw 90; panorama dry; land"))
        << out.str();
    ASSERT_TRUE(cli->execute_line("record status"));
    EXPECT_NE(out.str().find("4 lines"), std::string::npos) << out.str();
    ASSERT_TRUE(cli->execute_line("record stop"));
    EXPECT_NE(out.str().find("saved 4 lines to " + file.string()), std::string::npos) << out.str();

    std::ifstream in(file);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
    {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 5U);
    EXPECT_TRUE(lines[0].starts_with("# recorded by tello ")) << lines[0];
    EXPECT_EQ(std::vector<std::string>(lines.begin() + 1, lines.end()),
              (std::vector<std::string>{"takeoff", "forward 50", "cw 90", "land"}));

    {
        const std::scoped_lock lock(mutex);
        log.clear();
    }
    ASSERT_TRUE(cli->execute_line("mission " + file.string())) << out.str();
    EXPECT_EQ(commands(), (std::vector<std::string>{"takeoff", "forward 50", "cw 90", "land"}));
}

TEST_F(PhotoFigures, RecordRefusals)
{
    start();
    std::filesystem::create_directories(dir);
    cli->execute_line("record stop");
    EXPECT_NE(out.str().find("record: not recording"), std::string::npos) << out.str();
    cli->execute_line("record start");
    EXPECT_NE(out.str().find("usage: record start <file> | stop | status"), std::string::npos);
    cli->execute_line("record start /nonexistent_dir/route.txt");
    EXPECT_NE(out.str().find("record: cannot write"), std::string::npos) << out.str();
    ASSERT_TRUE(cli->execute_line("record start " + (dir / "a.txt").string()));
    cli->execute_line("record start " + (dir / "b.txt").string());
    EXPECT_NE(out.str().find("record: already recording"), std::string::npos) << out.str();
}

TEST(RouteTest, ManualFlightBecomesGoAndTurn)
{
    const drone::Position from{0, 0, 80, 90, 0};
    const drone::Position to{0, 100, 130, 0, 0};
    EXPECT_EQ(cli::route_commands(from, to, 30),
              (std::vector<std::string>{"go 100 0 50 30", "ccw 90"}));
    EXPECT_TRUE(cli::route_commands(from, from, 30).empty());
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

TEST_F(PhotoFigures, VideoRecordsTheWholeFigure)
{
    start(true);
    ASSERT_TRUE(cli->execute_line("flower 200 100 120 video")) << out.str();
    const std::string text = out.str();
    EXPECT_NE(text.find("camera in, video"), std::string::npos) << text;
    EXPECT_NE(text.find("flower: recording to "), std::string::npos) << text;
    EXPECT_NE(text.find("flower: saved "), std::string::npos) << text;
    EXPECT_LT(text.find("flower: saved "), text.find("flower: done, 3 of 3 photos"));
    int videos = 0;
    for (const auto& file : std::filesystem::directory_iterator(dir))
    {
        if (file.path().extension() == ".mp4")
        {
            ++videos;
            EXPECT_GT(file.file_size(), 0U);
        }
    }
    EXPECT_EQ(videos, 1);
    EXPECT_EQ(commands().back(), "land");
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
        {"panorama 200 25", "panorama step must divide 360, got 25"},
        {"panorama 200 30 in", "usage: panorama [height] [step] [cw|ccw]"},
        {"tower 300 200", "tower: from must be below to, got 300 and 200"},
        {"tower 100 500 10", "tower step (cm) must be 20..500, got 10"},
        {"dronie 2000", "dronie distance (cm) must be 50..1000, got 2000"},
        {"reveal 300 60", "reveal height (cm) must be 70..1000, got 60"},
        {"bounce 11", "bounce times must be 1..10, got 11"},
        {"flips l x", "usage: flips [l|r|f|b ...]"},
        {"flips 3", "usage: flips [l|r|f|b ...]"},
        {"helix 6", "helix turns must be 1..5, got 6"},
        {"helix 2 100 300 50 200 10", "10 degrees of a 50 cm circle"},
        {"boomerang 400 80", "boomerang width (cm) must be 100..1000, got 80"},
        {"polygon", "usage: polygon <corners>"},
        {"polygon 13", "polygon corners must be 3..12, got 13"},
        {"polygon 12 50", "polygon: a side of 26 cm is too short"},
        {"star 1 2 3 4", "usage: star [radius]"},
        {"rings 150 100 120 3", "rings: rings closer than 20 cm"},
        {"scan 40 100 300 50", "scan: step 50 cm is wider than the width 40 cm"},
        {"scan 300 300 100", "scan: from must not be above to"},
        {"timelapse 1", "timelapse interval (s) must be 2..60, got 1"},
        {"hyperlapse 100 10", "hyperlapse: 11 cm between frames"},
        {"eight 100 200 30 upright",
         "eight: the lowest point (height - radius) must be at least 50 cm, got 0"},
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

TEST_F(FigureDryRun, VideoFlagIsShownInTheSummary)
{
    cli.execute_line("loop video dry");
    EXPECT_TRUE(mock->requests.empty());
    EXPECT_NE(out.str().find("camera in, video\n"), std::string::npos) << out.str();
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
