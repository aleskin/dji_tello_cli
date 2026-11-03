// Camera stream: "video start/stop" through the CLI against the simulator, checked MP4 on disk.

#include "cli/command_processor.hpp"
#include "mock_transport.hpp"
#include "simulator.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_socket.hpp"
#include "tello/udp_transport.hpp"
#include "video/video_stream.hpp"

extern "C"
{
#include <libavformat/avformat.h>
}

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <thread>

namespace
{

using std::chrono::milliseconds;

// A free UDP port: FFmpeg binds by number, so take an ephemeral one and release it.
std::uint16_t free_udp_port()
{
    const tello::UdpSocket probe("127.0.0.1", 0);
    return probe.local_port();
}

struct Mp4Info
{
    bool opened = false;
    int width = 0;
    int height = 0;
    double seconds = 0;
    int packets = 0;
    bool first_is_key = false;
};

Mp4Info inspect(const std::filesystem::path& file)
{
    Mp4Info info;
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, file.string().c_str(), nullptr, nullptr) < 0)
    {
        return info;
    }
    if (avformat_find_stream_info(ctx, nullptr) >= 0 && ctx->nb_streams == 1)
    {
        info.opened = true;
        info.width = ctx->streams[0]->codecpar->width;
        info.height = ctx->streams[0]->codecpar->height;
        info.seconds = static_cast<double>(ctx->duration) / AV_TIME_BASE;
        AVPacket* pkt = av_packet_alloc();
        while (av_read_frame(ctx, pkt) >= 0)
        {
            if (info.packets == 0)
            {
                info.first_is_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            }
            ++info.packets;
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }
    avformat_close_input(&ctx);
    return info;
}

class VideoCliTest : public ::testing::Test
{
protected:
    VideoCliTest()
        : dir(std::filesystem::temp_directory_path() /
              ("tello_video_test_" + std::to_string(::getpid())))
    {
        const std::uint16_t video_port = free_udp_port();
        tello::sim::SimConfig config;
        config.command_port = 0;
        config.state_port = 0;
        config.video_port = video_port;
        sim = std::make_unique<tello::sim::Simulator>(config);
        drone = std::make_unique<tello::TelloClassic>(
            std::make_shared<tello::UdpTransport>("127.0.0.1", sim->command_port()),
            tello::DroneTimeouts{milliseconds(1000), milliseconds(500)});
        drone->set_video_port(video_port);
        drone->connect();
        cli = std::make_unique<cli::CommandProcessor>(
            *drone, out, cli::SessionInfo{"127.0.0.1", sim->command_port(), 0, dir});
    }
    ~VideoCliTest() override
    {
        cli.reset();
        std::filesystem::remove_all(dir);
    }

    std::filesystem::path dir;
    std::ostringstream out;
    std::unique_ptr<tello::sim::Simulator> sim;
    std::unique_ptr<tello::TelloClassic> drone;
    std::unique_ptr<cli::CommandProcessor> cli;
};

TEST_F(VideoCliTest, RecordsPlayableMp4)
{
    EXPECT_TRUE(cli->execute_line("video start"));
    EXPECT_NE(out.str().find("recording to " + dir.string()), std::string::npos);
    EXPECT_TRUE(sim->model().streaming);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    out.str("");
    cli->execute_line("video status");
    EXPECT_NE(out.str().find("receiving"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("960x720"), std::string::npos) << out.str();
    out.str("");
    cli->execute_line("video stop");
    EXPECT_NE(out.str().find("saved "), std::string::npos) << out.str();
    EXPECT_FALSE(sim->model().streaming);

    ASSERT_TRUE(std::filesystem::exists(dir));
    std::filesystem::path file;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        file = entry.path();
    }
    ASSERT_EQ(file.extension(), ".mp4");
    EXPECT_EQ(file.filename().string().rfind("tello_", 0), 0u);
    const Mp4Info info = inspect(file);
    ASSERT_TRUE(info.opened);
    EXPECT_EQ(info.width, 960);
    EXPECT_EQ(info.height, 720);
    EXPECT_TRUE(info.first_is_key);
    EXPECT_GE(info.packets, 30);  // ~2 s of 30 fps after the first keyframe
    EXPECT_GT(info.seconds, 1.0); // wall-clock timestamps survived the remux
}

TEST_F(VideoCliTest, StatusAndStopWithoutVideo)
{
    cli->execute_line("video");
    cli->execute_line("video stop");
    EXPECT_EQ(out.str(), "video is off\nvideo is off\n");
    cli->execute_line("video rewind");
    EXPECT_NE(out.str().find("usage: video start|stop|status"), std::string::npos);
}

TEST(VideoStreamTest, StopWithoutRecordingIsEmpty)
{
    video::VideoStream stream("udp://127.0.0.1:" + std::to_string(free_udp_port()));
    EXPECT_TRUE(stream.stop_recording().empty());
    EXPECT_FALSE(stream.status().receiving);
}

TEST(VideoStreamTest, DestructorStopsAWaitingReader)
{
    const auto start = std::chrono::steady_clock::now();
    {
        video::VideoStream stream("udp://127.0.0.1:" + std::to_string(free_udp_port()));
        stream.start_recording(std::filesystem::temp_directory_path() / "never_written.mp4");
        std::this_thread::sleep_for(milliseconds(200));
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(3));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::temp_directory_path() /
                                         "never_written.mp4"));
}

TEST(VideoModelTest, TelloStreamsViaStreamon)
{
    auto mock = std::make_shared<tello::test::MockTransport>();
    tello::TelloClassic drone(mock);
    EXPECT_TRUE(drone.supports(drone::Capability::Video));
    EXPECT_EQ(drone.video_source(), "udp://0.0.0.0:11111");
    drone.start_video();
    drone.stop_video();
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"streamon", "streamoff"}));
}

} // namespace
