#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace video
{

struct StreamStatus
{
    bool receiving = false;    // a packet arrived during the last second
    std::int64_t packets = 0;  // H.264 access units since the stream opened
    int width = 0;             // 0 until the stream parameters are known
    int height = 0;
    std::string recording;     // file being written, empty when not recording
    bool waiting_for_keyframe = false; // recording asked for, file opens at the next keyframe
    std::string error;         // last failure (open, write), empty if none
};

// Reads an H.264 stream (an FFmpeg URL such as "udp://0.0.0.0:11111") in a background thread
// and, on request, remuxes it into an MP4 file without re-encoding. Vendor-neutral: the drone
// only says where its stream is (DroneBase::video_source()).
class VideoStream
{
public:
    explicit VideoStream(std::string source);
    ~VideoStream(); // finishes a recording in progress, so the file stays playable

    VideoStream(const VideoStream&) = delete;
    VideoStream& operator=(const VideoStream&) = delete;
    VideoStream(VideoStream&&) = delete;
    VideoStream& operator=(VideoStream&&) = delete;

    // The file is created at the next keyframe once the stream is known (an MP4 must start
    // with a keyframe). A previous recording is finished first.
    void start_recording(const std::filesystem::path& path);

    // Writes the MP4 index and closes the file. Returns its path, empty if nothing was written.
    std::filesystem::path stop_recording();

    [[nodiscard]] StreamStatus status() const;

private:
    struct Input;  // FFmpeg demuxer state
    struct Output; // FFmpeg muxer state

    void run(const std::stop_token& stop);
    void open_output_locked(); // mutex_ held
    std::filesystem::path close_output_locked();

    std::string source_;
    // Polled by FFmpeg while it blocks on the socket; declared before input_ so it outlives it.
    std::atomic<bool> stopping_{false};
    mutable std::mutex mutex_; // everything below; the reader thread writes packets under it
    std::unique_ptr<Input> input_;
    std::unique_ptr<Output> output_;
    std::filesystem::path pending_path_;
    StreamStatus status_;
    std::chrono::steady_clock::time_point last_packet_{};
    std::jthread thread_; // last: started after everything it uses exists
};

// ~/Videos/tello/tello_YYYY-MM-DD_HH-MM-SS.mp4 (directory created if missing).
[[nodiscard]] std::filesystem::path default_recording_path(const std::filesystem::path& dir);

} // namespace video
