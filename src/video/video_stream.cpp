#include "video/video_stream.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
}

#include <array>
#include <ctime>
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

    Input() = default;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&&) = delete;
    Input& operator=(Input&&) = delete;
    ~Input()
    {
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
    if (st == nullptr ||
        avcodec_parameters_copy(st->codecpar, input_->stream()->codecpar) < 0)
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
    av_dict_set(&options, "fifo_size", "1000000", 0);    // 188-byte units: ~180 MB socket queue
    av_dict_set(&options, "overrun_nonfatal", "1", 0);   // drop on overrun instead of failing
    av_dict_set(&options, "buffer_size", "4194304", 0);  // kernel receive buffer
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
        const std::scoped_lock lock(mutex_);
        if (packet->stream_index == input_->stream_index)
        {
            ++status_.packets;
            last_packet_ = std::chrono::steady_clock::now();
            // AV_PKT_FLAG_KEY is an int macro: test the bit on an unsigned copy.
            const bool key = (static_cast<unsigned>(packet->flags) & static_cast<unsigned>(AV_PKT_FLAG_KEY)) != 0;
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
                std::int64_t ts = av_rescale_q(packet->dts - output_->first_dts, in->time_base,
                                               out->time_base);
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
