#pragma once

#include "video/viewer.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace video
{

// SDL2 window with the live picture and the OSD line. Resizable, the picture keeps its aspect.
// Runs its own thread (event loop and drawing); only one window per process, SDL is global.
class VideoWindow : public Viewer
{
public:
    explicit VideoWindow(ViewerSources sources, std::string title = "tello");
    ~VideoWindow() override;

    VideoWindow(const VideoWindow&) = delete;
    VideoWindow& operator=(const VideoWindow&) = delete;
    VideoWindow(VideoWindow&&) = delete;
    VideoWindow& operator=(VideoWindow&&) = delete;

    [[nodiscard]] bool is_open() const override;
    [[nodiscard]] std::string error() const override;
    [[nodiscard]] std::set<std::string> pressed_keys() const override;
    std::vector<std::string> take_key_presses() override;

    // Pictures drawn so far (tests).
    [[nodiscard]] std::int64_t pictures_shown() const
    {
        return shown_.load();
    }

private:
    void run(const std::stop_token& stop);

    ViewerSources sources_;
    std::string title_;
    std::atomic<bool> open_{true};
    std::atomic<std::int64_t> shown_{0};
    mutable std::mutex mutex_;
    std::string error_;
    std::set<std::string> pressed_;
    std::vector<std::string> presses_;
    std::jthread thread_; // last: started after everything it uses exists
};

} // namespace video
