#include "video_streamer.hpp"

#include "tello/errors.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace tello::sim
{
namespace
{

struct CodecContextDeleter
{
    void operator()(AVCodecContext* p) const
    {
        avcodec_free_context(&p);
    }
};
struct FrameDeleter
{
    void operator()(AVFrame* p) const
    {
        av_frame_free(&p);
    }
};
struct PacketDeleter
{
    void operator()(AVPacket* p) const
    {
        av_packet_free(&p);
    }
};

// Test pattern: a diagonal gradient that drifts, colour bands, and a white square moving
// across the picture, so a viewer can tell the stream is live and frames are in order.
void draw_pattern(AVFrame& frame, std::int64_t n)
{
    const int w = frame.width;
    const int h = frame.height;
    for (int y = 0; y < h; ++y)
    {
        std::uint8_t* row = frame.data[0] + static_cast<std::ptrdiff_t>(y) * frame.linesize[0];
        for (int x = 0; x < w; ++x)
        {
            row[x] = static_cast<std::uint8_t>((x + y + n * 4) % 256);
        }
    }
    for (int y = 0; y < h / 2; ++y)
    {
        std::uint8_t* u = frame.data[1] + static_cast<std::ptrdiff_t>(y) * frame.linesize[1];
        std::uint8_t* v = frame.data[2] + static_cast<std::ptrdiff_t>(y) * frame.linesize[2];
        for (int x = 0; x < w / 2; ++x)
        {
            const int band = (x * 8) / (w / 2);
            u[x] = static_cast<std::uint8_t>(64 + band * 16);
            v[x] = static_cast<std::uint8_t>(192 - band * 16);
        }
    }
    const int side = h / 6;
    const int left = static_cast<int>((n * 8) % (w - side));
    const int top = (h - side) / 2;
    for (int y = top; y < top + side; ++y)
    {
        std::uint8_t* row = frame.data[0] + static_cast<std::ptrdiff_t>(y) * frame.linesize[0];
        std::fill(row + left, row + left + side, std::uint8_t{235});
    }
}

} // namespace

struct VideoStreamer::Encoder
{
    std::unique_ptr<AVCodecContext, CodecContextDeleter> context;
    std::unique_ptr<AVFrame, FrameDeleter> frame;
    std::unique_ptr<AVPacket, PacketDeleter> packet;
};

VideoStreamer::VideoStreamer(std::string host, std::uint16_t port, std::atomic<int>& frames_sent,
                             VideoSettings settings)
    : host_(std::move(host))
    , port_(port)
    , frames_sent_(frames_sent)
    , settings_(settings)
    , socket_("0.0.0.0", 0)
    , encoder_(std::make_unique<Encoder>())
{
    // libx264 logs its banner and stats through FFmpeg; the simulator console is for commands.
    av_log_set_level(AV_LOG_ERROR);
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (codec == nullptr)
    {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (codec == nullptr)
    {
        throw TelloError("video: no H.264 encoder in libavcodec (install libx264)");
    }
    encoder_->context.reset(avcodec_alloc_context3(codec));
    AVCodecContext* ctx = encoder_->context.get();
    ctx->width = settings_.width;
    ctx->height = settings_.height;
    ctx->time_base = AVRational{1, settings_.fps};
    ctx->framerate = AVRational{settings_.fps, 1};
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->gop_size = settings_.fps; // a keyframe every second: a late viewer starts quickly
    ctx->max_b_frames = 0;
    ctx->bit_rate = settings_.bitrate;
    // libx264 only; other encoders ignore unknown private options.
    av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
    av_opt_set(ctx->priv_data, "x264-params", "repeat-headers=1", 0);
    if (avcodec_open2(ctx, codec, nullptr) < 0)
    {
        throw TelloError("video: cannot open the H.264 encoder");
    }

    encoder_->frame.reset(av_frame_alloc());
    encoder_->packet.reset(av_packet_alloc());
    AVFrame* frame = encoder_->frame.get();
    frame->format = ctx->pix_fmt;
    frame->width = ctx->width;
    frame->height = ctx->height;
    if (!frame || !encoder_->packet || av_frame_get_buffer(frame, 0) < 0)
    {
        throw TelloError("video: out of memory");
    }

    thread_ = std::jthread([this](const std::stop_token& stop) { run(stop); });
}

VideoStreamer::~VideoStreamer()
{
    thread_.request_stop();
    if (thread_.joinable())
    {
        thread_.join(); // before encoder_ goes away
    }
}

void VideoStreamer::run(const std::stop_token& stop)
{
    AVCodecContext* ctx = encoder_->context.get();
    AVFrame* frame = encoder_->frame.get();
    AVPacket* packet = encoder_->packet.get();
    const auto period = std::chrono::microseconds(1'000'000 / settings_.fps);
    auto next = std::chrono::steady_clock::now();

    for (std::int64_t n = 0; !stop.stop_requested(); ++n)
    {
        if (av_frame_make_writable(frame) < 0)
        {
            return;
        }
        draw_pattern(*frame, n);
        frame->pts = n;
        if (avcodec_send_frame(ctx, frame) < 0)
        {
            return;
        }
        while (avcodec_receive_packet(ctx, packet) == 0)
        {
            const std::span<const std::uint8_t> data(packet->data,
                                                     static_cast<std::size_t>(packet->size));
            for (std::size_t off = 0; off < data.size(); off += settings_.max_datagram)
            {
                const auto chunk = data.subspan(off, std::min(settings_.max_datagram,
                                                              data.size() - off));
                try
                {
                    socket_.send_to(std::string_view(reinterpret_cast<const char*>(chunk.data()),
                                                     chunk.size()),
                                    host_, port_);
                }
                catch (const TelloError&)
                {
                    // Nobody listening on the video port yet: UDP video is best effort.
                    break;
                }
            }
            av_packet_unref(packet);
        }
        ++frames_sent_;
        next += period;
        std::this_thread::sleep_until(next);
    }
}

} // namespace tello::sim
