#pragma once

#include "tello/udp_socket.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace tello::sim
{

// What the Tello camera sends after "streamon": 960x720 H.264 at 30 fps.
struct VideoSettings
{
    int width = 960;
    int height = 720;
    int fps = 30;
    int bitrate = 1'500'000;
    std::size_t max_datagram = 1460; // the drone splits the stream into chunks of this size
};

// Encodes a moving test pattern to H.264 (Annex B, SPS/PPS before every keyframe) and sends it
// to host:port in UDP chunks that ignore NAL boundaries, like the real drone. Starts in the
// constructor, stops in the destructor. Throws TelloError when no H.264 encoder is available.
class VideoStreamer
{
public:
    VideoStreamer(std::string host, std::uint16_t port, std::atomic<int>& frames_sent,
                  VideoSettings settings = {});
    ~VideoStreamer();

    VideoStreamer(const VideoStreamer&) = delete;
    VideoStreamer& operator=(const VideoStreamer&) = delete;
    VideoStreamer(VideoStreamer&&) = delete;
    VideoStreamer& operator=(VideoStreamer&&) = delete;

private:
    struct Encoder; // FFmpeg state, kept out of the header

    void run(const std::stop_token& stop);

    std::string host_;
    std::uint16_t port_;
    std::atomic<int>& frames_sent_;
    VideoSettings settings_;
    UdpSocket socket_;
    std::unique_ptr<Encoder> encoder_;
    std::jthread thread_; // last: started after everything it uses exists
};

} // namespace tello::sim
