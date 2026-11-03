// Simulator video: "streamon" starts an H.264 stream in UDP chunks that a real decoder accepts.

#include "simulator.hpp"
#include "tello/udp_socket.hpp"
#include "tello/udp_transport.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
}

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{

using std::chrono::milliseconds;

// Everything that arrives on the socket during `window`; datagram sizes in `sizes`.
std::string collect(const tello::UdpSocket& socket, milliseconds window,
                    std::vector<std::size_t>* sizes = nullptr)
{
    std::string stream;
    const auto until = std::chrono::steady_clock::now() + window;
    while (std::chrono::steady_clock::now() < until)
    {
        if (auto d = socket.receive(milliseconds(20)))
        {
            if (sizes != nullptr)
            {
                sizes->push_back(d->data.size());
            }
            stream += d->data;
        }
    }
    return stream;
}

struct Decoded
{
    int frames = 0;
    int width = 0;
    int height = 0;
};

// Feeds the byte stream through FFmpeg's H.264 parser and decoder, as TEL-00014 will.
Decoded decode(const std::string& stream)
{
    Decoded result;
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    std::unique_ptr<AVCodecParserContext, void (*)(AVCodecParserContext*)> parser(
        av_parser_init(AV_CODEC_ID_H264), av_parser_close);
    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext*)> ctx(
        avcodec_alloc_context3(codec),
        [](AVCodecContext* p)
        {
            avcodec_free_context(&p);
        });
    std::unique_ptr<AVPacket, void (*)(AVPacket*)> packet(av_packet_alloc(),
                                                          [](AVPacket* p)
                                                          {
                                                              av_packet_free(&p);
                                                          });
    std::unique_ptr<AVFrame, void (*)(AVFrame*)> frame(av_frame_alloc(),
                                                       [](AVFrame* p)
                                                       {
                                                           av_frame_free(&p);
                                                       });
    if (!parser || !ctx || !packet || !frame || avcodec_open2(ctx.get(), codec, nullptr) < 0)
    {
        return result;
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(stream.data());
    int left = static_cast<int>(stream.size());
    while (left > 0)
    {
        const int used = av_parser_parse2(parser.get(), ctx.get(), &packet->data, &packet->size,
                                          data, left, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        data += used;
        left -= used;
        if (packet->size == 0)
        {
            continue;
        }
        if (avcodec_send_packet(ctx.get(), packet.get()) == 0)
        {
            while (avcodec_receive_frame(ctx.get(), frame.get()) == 0)
            {
                ++result.frames;
                result.width = frame->width;
                result.height = frame->height;
            }
        }
    }
    return result;
}

class SimVideoTest : public ::testing::Test
{
protected:
    SimVideoTest()
    {
        tello::sim::SimConfig config;
        config.command_port = 0;
        config.state_port = 0;
        config.video_port = video.local_port();
        sim = std::make_unique<tello::sim::Simulator>(config);
        transport = std::make_unique<tello::UdpTransport>("127.0.0.1", sim->command_port());
    }

    tello::UdpSocket video{"127.0.0.1", 0};
    std::unique_ptr<tello::sim::Simulator> sim;
    std::unique_ptr<tello::UdpTransport> transport;
};

TEST(SimVideoLogicTest, StreamOnOffReplies)
{
    tello::sim::SimConfig config;
    tello::sim::SimLogic logic(config);
    const auto now = tello::sim::Clock::now();
    EXPECT_FALSE(logic.handle("streamon", now).has_value()); // not in SDK mode yet
    logic.handle("command", now);
    EXPECT_EQ(logic.handle("streamon", now), "ok");
    EXPECT_TRUE(logic.model().streaming);
    EXPECT_EQ(logic.handle("streamoff", now), "ok");
    EXPECT_FALSE(logic.model().streaming);
}

TEST_F(SimVideoTest, NothingBeforeStreamon)
{
    EXPECT_EQ(transport->request("command", milliseconds(500)), "ok");
    EXPECT_TRUE(collect(video, milliseconds(300)).empty());
}

TEST_F(SimVideoTest, StreamDecodesAs960x720)
{
    EXPECT_EQ(transport->request("command", milliseconds(500)), "ok");
    EXPECT_EQ(transport->request("streamon", milliseconds(500)), "ok");
    std::vector<std::size_t> sizes;
    const std::string stream = collect(video, milliseconds(1500), &sizes);
    ASSERT_FALSE(sizes.empty());
    for (const auto size : sizes)
    {
        EXPECT_LE(size, 1460u);
    }
    const Decoded decoded = decode(stream);
    EXPECT_GE(decoded.frames, 20); // ~30 fps over 1.5 s, minus the encoder warm-up
    EXPECT_EQ(decoded.width, 960);
    EXPECT_EQ(decoded.height, 720);
    EXPECT_GE(sim->video_frames_sent(), decoded.frames);
}

TEST_F(SimVideoTest, StreamoffStops)
{
    transport->request("command", milliseconds(500));
    transport->request("streamon", milliseconds(500));
    EXPECT_FALSE(collect(video, milliseconds(300)).empty());
    EXPECT_EQ(transport->request("streamoff", milliseconds(500)), "ok");
    collect(video, milliseconds(100)); // in flight before the stop
    EXPECT_TRUE(collect(video, milliseconds(300)).empty());
}

} // namespace
