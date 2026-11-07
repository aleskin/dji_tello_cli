// Camera stream: "video start/stop" through the CLI against the simulator, checked MP4 on disk.

#include "cli/command_processor.hpp"
#include "fake_viewer.hpp"
#include "mock_transport.hpp"
#include "simulator.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_socket.hpp"
#include "tello/udp_transport.hpp"
#include "video/video_stream.hpp"
#include "video/viewer.hpp"

extern "C"
{
#include <libavformat/avformat.h>
}

#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

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
            *drone, out,
            cli::SessionInfo{"127.0.0.1", sim->command_port(), 0, dir, dir / "photos"});
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

// A JPEG on disk with the camera's size, read back by FFmpeg.
std::pair<int, int> jpeg_size(const std::filesystem::path& file)
{
    AVFormatContext* ctx = nullptr;
    std::pair<int, int> size{0, 0};
    if (avformat_open_input(&ctx, file.string().c_str(), nullptr, nullptr) == 0)
    {
        if (avformat_find_stream_info(ctx, nullptr) >= 0 && ctx->nb_streams == 1 &&
            ctx->streams[0]->codecpar->codec_id == AV_CODEC_ID_MJPEG)
        {
            size = {ctx->streams[0]->codecpar->width, ctx->streams[0]->codecpar->height};
        }
        avformat_close_input(&ctx);
    }
    return size;
}

TEST_F(VideoCliTest, StalledRecordingDiskDoesNotHoldThePicture)
{
    std::filesystem::create_directories(dir);
    const auto fifo = dir / "stalled.mp4";
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
    std::optional<video::VideoStream> stream;
    stream.emplace(drone->video_source());
    drone->start_video();
    ASSERT_TRUE(stream->wait_for_frame(milliseconds(5000)));
    // Opening a FIFO nobody reads blocks: the reader thread hangs inside the recording code,
    // like a write to a stalled disk.
    stream->start_recording(fifo);
    std::atomic<bool> answered{false};
    std::thread probe(
        [&stream, &answered]
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (stream->status().waiting_for_keyframe &&
                   std::chrono::steady_clock::now() < until)
            {
                std::this_thread::sleep_for(milliseconds(10));
            }
            std::this_thread::sleep_for(milliseconds(200));
            answered.store(stream->latest_picture(0).has_value());
        });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(7);
    while (!answered.load() && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(milliseconds(10));
    }
    EXPECT_TRUE(answered.load()) << "the picture waited for the stalled recording";
    // Let the writer go: read the FIFO until the stream closes it.
    const int reader = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
    ASSERT_GE(reader, 0);
    ::fcntl(reader, F_SETFL, 0);
    std::thread drain(
        [reader]
        {
            std::array<char, 4096> buf{};
            while (::read(reader, buf.data(), buf.size()) > 0)
            {
            }
        });
    probe.join();
    stream.reset();
    drain.join();
    ::close(reader);
    drone->stop_video();
}

TEST_F(VideoCliTest, PhotoStartsTheCameraAndSavesJpeg)
{
    EXPECT_TRUE(cli->execute_line("photo"));
    EXPECT_NE(out.str().find("saved " + (dir / "photos").string()), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("camera stays on"), std::string::npos);
    EXPECT_TRUE(sim->model().streaming);
    out.str("");
    EXPECT_TRUE(cli->execute_line("photo; photo")); // camera already on: immediate, no clash
    EXPECT_EQ(out.str().find("camera stays on"), std::string::npos);

    int photos = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir / "photos"))
    {
        ++photos;
        EXPECT_EQ(entry.path().extension(), ".jpg");
        EXPECT_EQ(jpeg_size(entry.path()), (std::pair<int, int>{960, 720}));
        EXPECT_GT(std::filesystem::file_size(entry.path()), 10'000u);
    }
    EXPECT_EQ(photos, 3);
    cli->execute_line("video stop");
    EXPECT_FALSE(sim->model().streaming);
}

TEST(VideoStreamTest, PhotoWithoutPictureThrows)
{
    video::VideoStream stream("udp://127.0.0.1:" + std::to_string(free_udp_port()));
    EXPECT_FALSE(stream.wait_for_frame(milliseconds(100)));
    EXPECT_THROW(stream.save_photo(std::filesystem::temp_directory_path() / "none.jpg"),
                 std::runtime_error);
}

TEST(VideoPathTest, PhotoNamesAreUniquePerMillisecond)
{
    const auto dir = std::filesystem::temp_directory_path() / "tello_photo_names";
    const auto a = video::default_photo_path(dir);
    std::this_thread::sleep_for(milliseconds(2));
    const auto b = video::default_photo_path(dir);
    EXPECT_NE(a, b);
    EXPECT_EQ(a.extension(), ".jpg");
    std::filesystem::remove_all(dir);
}

TEST_F(VideoCliTest, StatusAndStopWithoutVideo)
{
    cli->execute_line("video");
    cli->execute_line("video stop");
    EXPECT_EQ(out.str(), "video is off\nvideo is off\n");
    cli->execute_line("video rewind");
    EXPECT_NE(out.str().find("usage: video start|stop|show|hide|status"), std::string::npos);
}

using tello::test::FakeViewer;

TEST_F(VideoCliTest, ShowWithoutWindowSupportIsRefused)
{
    EXPECT_TRUE(cli->execute_line("video show"));
    EXPECT_NE(out.str().find("no window support"), std::string::npos) << out.str();
    EXPECT_FALSE(sim->model().streaming);
}

TEST_F(VideoCliTest, ShowAndHideTheWindow)
{
    int alive = 0;
    FakeViewer* viewer = nullptr;
    cli->set_viewer_factory(
        [&alive, &viewer](video::ViewerSources s) -> std::unique_ptr<video::Viewer>
        {
            auto v = std::make_unique<FakeViewer>(std::move(s), alive);
            viewer = v.get();
            return v;
        });
    EXPECT_TRUE(cli->execute_line("video show"));
    EXPECT_NE(out.str().find("window open"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("camera stays on"), std::string::npos);
    EXPECT_EQ(alive, 1);
    EXPECT_TRUE(sim->model().streaming);

    // The window gets real pictures from the stream and an empty OSD without telemetry.
    std::optional<video::Picture> picture;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!picture && std::chrono::steady_clock::now() < until)
    {
        picture = viewer->sources.picture(0);
        std::this_thread::sleep_for(milliseconds(20));
    }
    ASSERT_TRUE(picture);
    EXPECT_EQ(picture->width, 960);
    EXPECT_EQ(picture->y.size(), 960u * 720u);
    EXPECT_EQ(picture->u.size(), 480u * 360u);
    EXPECT_FALSE(viewer->sources.picture(picture->number + 1'000'000)); // nothing newer
    EXPECT_FALSE(viewer->sources.osd().battery);

    out.str("");
    cli->execute_line("video show");
    EXPECT_EQ(out.str(), "window already open\n");
    out.str("");
    cli->execute_line("video status");
    EXPECT_NE(out.str().find("window open"), std::string::npos) << out.str();

    viewer->open = false; // the user closed it: "show" opens a new one
    cli->execute_line("video show");
    EXPECT_EQ(alive, 1);
    out.str("");
    cli->execute_line("video hide; video hide");
    EXPECT_EQ(out.str(), "window closed\nno window\n");
    EXPECT_EQ(alive, 0);
    EXPECT_TRUE(sim->model().streaming); // hiding the window keeps the camera on

    cli->execute_line("video show; video stop");
    EXPECT_EQ(alive, 0);
    EXPECT_FALSE(sim->model().streaming);
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
    EXPECT_FALSE(
        std::filesystem::exists(std::filesystem::temp_directory_path() / "never_written.mp4"));
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
