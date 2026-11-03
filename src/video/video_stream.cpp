#include "video/video_stream.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
}

#include <array>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace video
{
namespace
{

std::string av_error(int code)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buf{};
    av_strerror(code, buf.data(), buf.size());
    return buf.data();
}

struct PacketDeleter
{
    void operator()(AVPacket* p) const
    {
        av_packet_free(&p);
    }
};

} // namespace

struct VideoStream::Input
{
    AVFormatContext* context = nullptr;
    int stream_index = -1;
    AVCodecContext* decoder = nullptr; // reader thread only
    AVFrame* scratch = nullptr;        // reader thread only
    AVFrame* latest = nullptr;         // last decoded picture, guarded by VideoStream::mutex_

    Input() = default;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&&) = delete;
    Input& operator=(Input&&) = delete;
    ~Input()
    {
        av_frame_free(&latest);
        av_frame_free(&scratch);
        avcodec_free_context(&decoder);
        avformat_close_input(&context);
    }

    [[nodiscard]] AVStream* stream() const
    {
        return context->streams[stream_index];
    }
};

struct VideoStream::Output
{
    AVFormatContext* context = nullptr;
    std::filesystem::path path;
    std::int64_t first_dts = AV_NOPTS_VALUE; // in the input time base
    std::int64_t last_dts = AV_NOPTS_VALUE;  // in the output time base
    bool header_written = false;

    Output() = default;
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;
    ~Output()
    {
        if (context == nullptr)
        {
            return;
        }
        if (header_written)
        {
            av_write_trailer(context); // the MP4 index: without it the file does not play
        }
        avio_closep(&context->pb);
        avformat_free_context(context);
    }
};

VideoStream::VideoStream(std::string source)
    : source_(std::move(source))
    , thread_(
          [this](const std::stop_token& stop)
          {
              run(stop);
          })
{
}

VideoStream::~VideoStream()
{
    stopping_.store(true);
    thread_.request_stop();
    if (thread_.joinable())
    {
        thread_.join();
    }
    const std::scoped_lock lock(mutex_);
    close_output_locked();
}

void VideoStream::start_recording(const std::filesystem::path& path)
{
    const std::scoped_lock lock(mutex_);
    close_output_locked();
    pending_path_ = path;
    status_.waiting_for_keyframe = true;
    status_.error.clear();
}

std::filesystem::path VideoStream::stop_recording()
{
    const std::scoped_lock lock(mutex_);
    pending_path_.clear();
    status_.waiting_for_keyframe = false;
    return close_output_locked();
}

StreamStatus VideoStream::status() const
{
    const std::scoped_lock lock(mutex_);
    StreamStatus s = status_;
    s.receiving = std::chrono::steady_clock::now() - last_packet_ < std::chrono::seconds(1);
    return s;
}

std::filesystem::path VideoStream::close_output_locked()
{
    if (!output_)
    {
        return {};
    }
    const bool wrote = output_->header_written;
    std::filesystem::path path = output_->path;
    output_.reset();
    status_.recording.clear();
    return wrote ? path : std::filesystem::path{};
}

void VideoStream::open_output_locked()
{
    auto out = std::make_unique<Output>();
    out->path = pending_path_;
    pending_path_.clear();
    status_.waiting_for_keyframe = false;

    const std::string path = out->path.string();
    int rc = avformat_alloc_output_context2(&out->context, nullptr, "mp4", path.c_str());
    if (rc < 0 || out->context == nullptr)
    {
        status_.error = "cannot create " + path + ": " + av_error(rc);
        return;
    }
    AVStream* st = avformat_new_stream(out->context, nullptr);
    if (st == nullptr || avcodec_parameters_copy(st->codecpar, input_->stream()->codecpar) < 0)
    {
        status_.error = "cannot set up the MP4 stream";
        return;
    }
    st->codecpar->codec_tag = 0;
    st->time_base = AVRational{1, 90'000};
    rc = avio_open(&out->context->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (rc < 0)
    {
        status_.error = "cannot open " + path + ": " + av_error(rc);
        return;
    }
    rc = avformat_write_header(out->context, nullptr);
    if (rc < 0)
    {
        status_.error = "cannot write MP4 header: " + av_error(rc);
        return;
    }
    out->header_written = true;
    status_.recording = path;
    output_ = std::move(out);
}

void VideoStream::run(const std::stop_token& stop)
{
    av_log_set_level(AV_LOG_ERROR);
    auto input = std::make_unique<Input>();
    input->context = avformat_alloc_context();
    if (input->context == nullptr)
    {
        const std::scoped_lock lock(mutex_);
        status_.error = "out of memory";
        return;
    }
    // FFmpeg polls this while it blocks on the socket: the destructor can always stop the thread.
    input->context->interrupt_callback.callback = [](void* opaque) -> int
    {
        return static_cast<const std::atomic<bool>*>(opaque)->load() ? 1 : 0;
    };
    input->context->interrupt_callback.opaque = &stopping_;

    AVDictionary* options = nullptr;
    // The drone sends no timestamps: take the arrival time, which also keeps pauses in the file.
    av_dict_set(&options, "use_wallclock_as_timestamps", "1", 0);
    av_dict_set(&options, "fifo_size", "1000000", 0);   // 188-byte units: ~180 MB socket queue
    av_dict_set(&options, "overrun_nonfatal", "1", 0);  // drop on overrun instead of failing
    av_dict_set(&options, "buffer_size", "4194304", 0); // kernel receive buffer
    const AVInputFormat* format = av_find_input_format("h264");
    int rc = avformat_open_input(&input->context, source_.c_str(), format, &options);
    av_dict_free(&options);
    if (rc < 0)
    {
        input->context = nullptr; // freed by avformat_open_input on failure
        const std::scoped_lock lock(mutex_);
        status_.error = "cannot open " + source_ + ": " + av_error(rc);
        return;
    }
    rc = avformat_find_stream_info(input->context, nullptr); // waits for SPS/PPS and a keyframe
    if (rc < 0 || stop.stop_requested())
    {
        const std::scoped_lock lock(mutex_);
        if (!stop.stop_requested())
        {
            status_.error = "no H.264 stream on " + source_ + ": " + av_error(rc);
        }
        return;
    }
    input->stream_index =
        av_find_best_stream(input->context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (input->stream_index < 0)
    {
        const std::scoped_lock lock(mutex_);
        status_.error = "no video in " + source_;
        return;
    }
    // Decoder for photo (and the live window later); recording does not need it.
    const AVCodec* h264 = avcodec_find_decoder(input->stream()->codecpar->codec_id);
    input->decoder = h264 != nullptr ? avcodec_alloc_context3(h264) : nullptr;
    input->scratch = av_frame_alloc();
    input->latest = av_frame_alloc();
    if (input->decoder == nullptr || input->scratch == nullptr || input->latest == nullptr ||
        avcodec_parameters_to_context(input->decoder, input->stream()->codecpar) < 0 ||
        avcodec_open2(input->decoder, h264, nullptr) < 0)
    {
        const std::scoped_lock lock(mutex_);
        status_.error = "cannot open the H.264 decoder";
        return;
    }
    {
        const std::scoped_lock lock(mutex_);
        status_.width = input->stream()->codecpar->width;
        status_.height = input->stream()->codecpar->height;
        input_ = std::move(input);
    }

    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    while (!stop.stop_requested())
    {
        rc = av_read_frame(input_->context, packet.get());
        if (rc < 0)
        {
            if (stop.stop_requested())
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // EAGAIN or a hiccup
            continue;
        }
        if (packet->stream_index != input_->stream_index)
        {
            av_packet_unref(packet.get());
            continue;
        }
        // Decode outside the lock; only the hand-over of the finished picture is guarded.
        if (avcodec_send_packet(input_->decoder, packet.get()) == 0)
        {
            while (avcodec_receive_frame(input_->decoder, input_->scratch) == 0)
            {
                const std::scoped_lock lock(mutex_);
                av_frame_unref(input_->latest);
                av_frame_move_ref(input_->latest, input_->scratch);
                ++status_.frames;
            }
        }
        const std::scoped_lock lock(mutex_);
        {
            ++status_.packets;
            last_packet_ = std::chrono::steady_clock::now();
            // AV_PKT_FLAG_KEY is an int macro: test the bit on an unsigned copy.
            const bool key = (static_cast<unsigned>(packet->flags) &
                              static_cast<unsigned>(AV_PKT_FLAG_KEY)) != 0;
            if (!output_ && !pending_path_.empty() && key)
            {
                open_output_locked();
            }
            if (output_ && packet->dts != AV_NOPTS_VALUE)
            {
                AVStream* in = input_->stream();
                AVStream* out = output_->context->streams[0];
                if (output_->first_dts == AV_NOPTS_VALUE)
                {
                    output_->first_dts = packet->dts;
                }
                std::int64_t ts =
                    av_rescale_q(packet->dts - output_->first_dts, in->time_base, out->time_base);
                if (output_->last_dts != AV_NOPTS_VALUE && ts <= output_->last_dts)
                {
                    ts = output_->last_dts + 1; // MP4 needs strictly increasing timestamps
                }
                output_->last_dts = ts;
                packet->pts = ts; // no B-frames: presentation order is decode order
                packet->dts = ts;
                packet->duration = 0;
                packet->stream_index = 0;
                packet->pos = -1;
                rc = av_interleaved_write_frame(output_->context, packet.get());
                if (rc < 0)
                {
                    status_.error = "write failed: " + av_error(rc);
                    close_output_locked();
                }
            }
        }
        av_packet_unref(packet.get());
    }
}

bool VideoStream::wait_for_frame(std::chrono::milliseconds timeout) const
{
    const auto until = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
        {
            const std::scoped_lock lock(mutex_);
            if (status_.frames > 0)
            {
                return true;
            }
        }
        if (std::chrono::steady_clock::now() >= until)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void VideoStream::save_photo(const std::filesystem::path& path) const
{
    struct FrameDeleter
    {
        void operator()(AVFrame* p) const
        {
            av_frame_free(&p);
        }
    };
    struct ContextDeleter
    {
        void operator()(AVCodecContext* p) const
        {
            avcodec_free_context(&p);
        }
    };

    std::unique_ptr<AVFrame, FrameDeleter> frame;
    {
        const std::scoped_lock lock(mutex_);
        if (!input_ || status_.frames == 0)
        {
            throw std::runtime_error("no picture yet: is the stream running?");
        }
        frame.reset(av_frame_clone(input_->latest)); // a reference, not a copy of the pixels
    }
    if (!frame || frame->format != AV_PIX_FMT_YUV420P)
    {
        throw std::runtime_error("photo: unexpected picture format");
    }

    const AVCodec* mjpeg = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    std::unique_ptr<AVCodecContext, ContextDeleter> ctx(
        mjpeg != nullptr ? avcodec_alloc_context3(mjpeg) : nullptr);
    if (!ctx)
    {
        throw std::runtime_error("photo: no JPEG encoder in libavcodec");
    }
    ctx->width = frame->width;
    ctx->height = frame->height;
    ctx->time_base = AVRational{1, 25};
    // Full-range 4:2:0 is the JPEG layout of the decoded picture; only the range flag differs.
    ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    ctx->color_range = AVCOL_RANGE_JPEG;
    ctx->flags =
        static_cast<int>(static_cast<unsigned>(ctx->flags) |
                         static_cast<unsigned>(AV_CODEC_FLAG_QSCALE)); // FFmpeg flags are int
    ctx->global_quality = FF_QP2LAMBDA * 3; // high quality, ~100-200 KB at 960x720
    if (avcodec_open2(ctx.get(), mjpeg, nullptr) < 0)
    {
        throw std::runtime_error("photo: cannot open the JPEG encoder");
    }
    frame->format = AV_PIX_FMT_YUVJ420P;
    frame->pts = 0;
    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    if (!packet || avcodec_send_frame(ctx.get(), frame.get()) < 0 ||
        avcodec_send_frame(ctx.get(), nullptr) < 0 ||
        avcodec_receive_packet(ctx.get(), packet.get()) < 0)
    {
        throw std::runtime_error("photo: JPEG encoding failed");
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(packet->data), packet->size);
    if (!file)
    {
        throw std::runtime_error("photo: cannot write " + path.string());
    }
}

std::filesystem::path default_photo_path(const std::filesystem::path& dir)
{
    std::filesystem::create_directories(dir);
    const auto now = std::chrono::system_clock::now();
    const std::time_t secs = std::chrono::system_clock::to_time_t(now);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    std::tm local{};
    localtime_r(&secs, &local);
    std::array<char, 64> name{};
    const std::size_t n =
        std::strftime(name.data(), name.size(), "tello_%Y-%m-%d_%H-%M-%S", &local);
    std::array<char, 16> suffix{};
    std::snprintf(suffix.data(), suffix.size(), "_%03d.jpg", static_cast<int>(ms));
    return dir / (std::string(name.data(), n) + suffix.data());
}

std::filesystem::path default_recording_path(const std::filesystem::path& dir)
{
    std::filesystem::create_directories(dir);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 64> name{};
    std::strftime(name.data(), name.size(), "tello_%Y-%m-%d_%H-%M-%S.mp4", &local);
    return dir / name.data();
}

} // namespace video
