#include "video/video_stream.hpp"

#include "applog/log.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace video
{
namespace
{

constexpr auto kVideoGap = std::chrono::seconds(1); // longer silence goes to the log

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

std::vector<std::uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
    if (!in.is_open() || data.empty())
    {
        throw std::runtime_error("movie: cannot read " + path.string());
    }
    return data;
}

// Width and height from the SOF segment of a JPEG.
std::pair<int, int> jpeg_size(const std::vector<std::uint8_t>& jpeg)
{
    std::size_t i = 2; // after SOI
    while (i + 9 < jpeg.size() && jpeg[i] == 0xFF)
    {
        const std::uint8_t marker = jpeg[i + 1];
        const std::size_t length = (static_cast<std::size_t>(jpeg[i + 2]) << 8U) | jpeg[i + 3];
        if (marker >= 0xC0 && marker <= 0xC3)
        {
            const auto height =
                static_cast<int>((static_cast<unsigned>(jpeg[i + 5]) << 8U) | jpeg[i + 6]);
            const auto width =
                static_cast<int>((static_cast<unsigned>(jpeg[i + 7]) << 8U) | jpeg[i + 8]);
            return {width, height};
        }
        i += 2 + length;
    }
    throw std::runtime_error("movie: not a JPEG picture");
}

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
    applog::info("video", "closed " + source_ + " after " + std::to_string(status_.packets) +
                              " packets, " + std::to_string(status_.frames) + " frames");
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
    applog::info("video", "recording closed: " + path.string());
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
        applog::error("video", status_.error);
        return;
    }
    AVStream* st = avformat_new_stream(out->context, nullptr);
    if (st == nullptr || avcodec_parameters_copy(st->codecpar, input_->stream()->codecpar) < 0)
    {
        status_.error = "cannot set up the MP4 stream";
        applog::error("video", status_.error);
        return;
    }
    st->codecpar->codec_tag = 0;
    st->time_base = AVRational{1, 90'000};
    rc = avio_open(&out->context->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (rc < 0)
    {
        status_.error = "cannot open " + path + ": " + av_error(rc);
        applog::error("video", status_.error);
        return;
    }
    rc = avformat_write_header(out->context, nullptr);
    if (rc < 0)
    {
        status_.error = "cannot write MP4 header: " + av_error(rc);
        applog::error("video", status_.error);
        return;
    }
    out->header_written = true;
    status_.recording = path;
    applog::info("video", "recording to " + path);
    output_ = std::move(out);
}

void VideoStream::run(const std::stop_token& stop)
{
    applog::set_thread_name("video");
    applog::info("video", "opening " + source_);
    av_log_set_level(AV_LOG_ERROR);
    auto input = std::make_unique<Input>();
    input->context = avformat_alloc_context();
    if (input->context == nullptr)
    {
        const std::scoped_lock lock(mutex_);
        status_.error = "out of memory";
        applog::error("video", status_.error);
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
        applog::error("video", status_.error);
        return;
    }
    rc = avformat_find_stream_info(input->context, nullptr); // waits for SPS/PPS and a keyframe
    if (rc < 0 || stop.stop_requested())
    {
        const std::scoped_lock lock(mutex_);
        if (!stop.stop_requested())
        {
            status_.error = "no H.264 stream on " + source_ + ": " + av_error(rc);
            applog::error("video", status_.error);
        }
        return;
    }
    input->stream_index =
        av_find_best_stream(input->context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (input->stream_index < 0)
    {
        const std::scoped_lock lock(mutex_);
        status_.error = "no video in " + source_;
        applog::error("video", status_.error);
        return;
    }
    // Decoder for photo and the live window; recording does not need it.
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
        applog::error("video", status_.error);
        return;
    }
    {
        const std::scoped_lock lock(mutex_);
        status_.width = input->stream()->codecpar->width;
        status_.height = input->stream()->codecpar->height;
        input_ = std::move(input);
        applog::info("video", "stream " + std::to_string(status_.width) + "x" +
                                  std::to_string(status_.height) + " on " + source_);
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
            const auto now = std::chrono::steady_clock::now();
            if (status_.packets > 0 && now - last_packet_ > kVideoGap)
            {
                applog::warn("video", "no packets for " + applog::ms(now - last_packet_));
            }
            ++status_.packets;
            last_packet_ = now;
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
                    applog::error("video", status_.error);
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

std::optional<Picture> VideoStream::latest_picture(std::int64_t newer_than) const
{
    const std::scoped_lock lock(mutex_);
    if (!input_ || status_.frames <= newer_than || input_->latest->format != AV_PIX_FMT_YUV420P)
    {
        return std::nullopt;
    }
    const AVFrame* f = input_->latest;
    Picture picture;
    picture.width = f->width;
    picture.height = f->height;
    picture.number = status_.frames;
    auto copy_plane = [f](int plane, int w, int h, std::vector<std::uint8_t>& to)
    {
        to.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        for (int row = 0; row < h; ++row)
        {
            const std::uint8_t* from =
                f->data[plane] + static_cast<std::ptrdiff_t>(row) * f->linesize[plane];
            std::copy(from, from + w, to.begin() + static_cast<std::ptrdiff_t>(row) * w);
        }
    };
    copy_plane(0, f->width, f->height, picture.y);
    copy_plane(1, (f->width + 1) / 2, (f->height + 1) / 2, picture.u);
    copy_plane(2, (f->width + 1) / 2, (f->height + 1) / 2, picture.v);
    return picture;
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
    applog::info("video", "photo " + path.string());
}

void make_movie(const std::vector<std::filesystem::path>& frames, const std::filesystem::path& path,
                int fps)
{
    struct Muxer
    {
        AVFormatContext* context = nullptr;
        bool header_written = false;

        Muxer() = default;
        Muxer(const Muxer&) = delete;
        Muxer& operator=(const Muxer&) = delete;
        Muxer(Muxer&&) = delete;
        Muxer& operator=(Muxer&&) = delete;
        ~Muxer()
        {
            if (context == nullptr)
            {
                return;
            }
            if (header_written)
            {
                av_write_trailer(context);
            }
            avio_closep(&context->pb);
            avformat_free_context(context);
        }
    };

    if (frames.empty())
    {
        throw std::runtime_error("movie: no frames");
    }
    const auto first = read_file(frames.front());
    const auto [width, height] = jpeg_size(first);
    const std::string name = path.string();
    Muxer mux;
    int rc = avformat_alloc_output_context2(&mux.context, nullptr, "mp4", name.c_str());
    if (rc < 0 || mux.context == nullptr)
    {
        throw std::runtime_error("movie: cannot create " + name + ": " + av_error(rc));
    }
    AVStream* st = avformat_new_stream(mux.context, nullptr);
    if (st == nullptr)
    {
        throw std::runtime_error("movie: cannot set up the MP4 stream");
    }
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_MJPEG;
    st->codecpar->width = width;
    st->codecpar->height = height;
    st->time_base = AVRational{1, fps};
    rc = avio_open(&mux.context->pb, name.c_str(), AVIO_FLAG_WRITE);
    if (rc < 0)
    {
        throw std::runtime_error("movie: cannot open " + name + ": " + av_error(rc));
    }
    rc = avformat_write_header(mux.context, nullptr);
    if (rc < 0)
    {
        throw std::runtime_error("movie: cannot write the MP4 header: " + av_error(rc));
    }
    mux.header_written = true;
    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    if (!packet)
    {
        throw std::runtime_error("movie: out of memory");
    }
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        const auto jpeg = i == 0 ? first : read_file(frames[i]);
        if (jpeg_size(jpeg) != std::pair{width, height})
        {
            throw std::runtime_error("movie: " + frames[i].string() + " has another size");
        }
        if (av_new_packet(packet.get(), static_cast<int>(jpeg.size())) < 0)
        {
            throw std::runtime_error("movie: out of memory");
        }
        std::ranges::copy(jpeg, packet->data);
        packet->stream_index = st->index;
        packet->pts = av_rescale_q(static_cast<std::int64_t>(i), AVRational{1, fps}, st->time_base);
        packet->dts = packet->pts;
        packet->duration = av_rescale_q(1, AVRational{1, fps}, st->time_base);
        packet->flags = static_cast<int>(static_cast<unsigned>(packet->flags) |
                                         static_cast<unsigned>(AV_PKT_FLAG_KEY));
        rc = av_interleaved_write_frame(mux.context, packet.get());
        av_packet_unref(packet.get());
        if (rc < 0)
        {
            throw std::runtime_error("movie: cannot write a frame: " + av_error(rc));
        }
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
