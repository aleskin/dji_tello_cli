#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace video
{

struct StreamStatus
{
    bool receiving = false;   // a packet arrived during the last second
    std::int64_t packets = 0; // H.264 access units since the stream opened
    std::int64_t frames = 0;  // pictures decoded (the last one is what photo saves)
    int width = 0;            // 0 until the stream parameters are known
    int height = 0;
    std::string recording;             // file being written, empty when not recording
    bool waiting_for_keyframe = false; // recording asked for, file opens at the next keyframe
    std::string error;                 // last failure (open, write), empty if none
};

// A decoded camera picture, YUV 4:2:0 planes packed without padding (u and v are half size).
struct Picture
{
    int width = 0;
    int height = 0;
    std::int64_t number = 0; // StreamStatus::frames when it was decoded
    std::vector<std::uint8_t> y;
    std::vector<std::uint8_t> u;
    std::vector<std::uint8_t> v;
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

    // Saves the last decoded picture as JPEG. Throws std::runtime_error when there is none yet
    // or encoding fails.
    void save_photo(const std::filesystem::path& path) const;

    // A copy of the last decoded picture if it is newer than `newer_than` (a Picture::number),
    // std::nullopt otherwise: a viewer polls with the number it already shows.
    [[nodiscard]] std::optional<Picture> latest_picture(std::int64_t newer_than = 0) const;

    // Blocks until a picture has been decoded or `timeout` passed; true if there is one.
    bool wait_for_frame(std::chrono::milliseconds timeout) const;

private:
    struct Input;  // FFmpeg demuxer state
    struct Output; // FFmpeg muxer state

    void run(const std::stop_token& stop);
    void open_output_locked(); // output_mutex_ held
    std::filesystem::path close_output_locked();
    void set_error(const std::string& error); // takes mutex_

    std::string source_;
    // Polled by FFmpeg while it blocks on the socket; declared before input_ so it outlives it.
    std::atomic<bool> stopping_{false};
    // The recording: file I/O (which can stall on a slow disk) happens under this lock only, so
    // the picture and the status stay available. Taken before mutex_ when both are needed.
    std::mutex output_mutex_;
    std::unique_ptr<Output> output_;
    std::filesystem::path pending_path_;
    mutable std::mutex mutex_; // the decoded picture and the status below
    std::unique_ptr<Input> input_;
    StreamStatus status_;
    std::chrono::steady_clock::time_point last_packet_{};
    std::jthread thread_; // last: started after everything it uses exists
};

// dir/tello_YYYY-MM-DD_HH-MM-SS.mp4 (directory created if missing).
[[nodiscard]] std::filesystem::path default_recording_path(const std::filesystem::path& dir);

// dir/tello_YYYY-MM-DD_HH-MM-SS_mmm.jpg: milliseconds, so photos in one second do not collide.
[[nodiscard]] std::filesystem::path default_photo_path(const std::filesystem::path& dir);

// JPEG frames (all of one size) into an MP4 movie at fps frames per second, without re-encoding.
// Throws std::runtime_error.
void make_movie(const std::vector<std::filesystem::path>& frames, const std::filesystem::path& path,
                int fps);

} // namespace video
