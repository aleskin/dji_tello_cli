#pragma once

#include "video/video_stream.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace video
{

// On-screen display over the live picture; a missing value is shown as "--".
struct Osd
{
    std::optional<int> battery;     // percent
    std::optional<int> height_cm;   // above the takeoff point
    std::optional<int> heading_deg; // 0..359
    std::string mode;               // e.g. "KEYBOARD", empty in the command mode
};

// "BAT 87%  H 120CM  HDG 045  KEYBOARD": the line drawn over the picture.
[[nodiscard]] std::string osd_text(const Osd& osd);

// Telemetry yaw (-180..180) as a compass-style heading 0..359.
[[nodiscard]] int heading_from_yaw(int yaw);

// A game controller as the window sees it. Sticks are -1..1 with up and right positive, raw
// (no dead zone); buttons are lowercase SDL names: "a", "b", "start", "back", "dpup".
struct Gamepad
{
    std::string name;
    double left_x = 0.0;
    double left_y = 0.0;
    double right_x = 0.0;
    double right_y = 0.0;
    std::set<std::string> buttons; // held down right now
};

// Where a viewer takes its picture (polled with the number it already shows) and its OSD.
struct ViewerSources
{
    std::function<std::optional<Picture>(std::int64_t newer_than)> picture;
    std::function<Osd()> osd;
};

// A live picture window. Vendor- and toolkit-neutral: the CLI owns one through this interface,
// the SDL implementation (VideoWindow) is plugged in by the executable.
class Viewer
{
public:
    Viewer() = default;
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;
    Viewer(Viewer&&) = delete;
    Viewer& operator=(Viewer&&) = delete;
    virtual ~Viewer() = default; // closes the window

    // False once the user closed the window or it could not be opened (see error()).
    [[nodiscard]] virtual bool is_open() const = 0;
    [[nodiscard]] virtual std::string error() const = 0;

    // Keys held down in the window right now, lowercase SDL names: "w", "up", "escape".
    // Empty while the window has no focus, so switching away releases every key.
    [[nodiscard]] virtual std::set<std::string> pressed_keys() const = 0;

    // Keys pressed since the last call, in order: a tap shorter than the poll is not lost.
    virtual std::vector<std::string> take_key_presses() = 0;

    // The first connected game controller; nullopt when there is none (or no support).
    [[nodiscard]] virtual std::optional<Gamepad> gamepad() const
    {
        return std::nullopt;
    }

    // Controller buttons pressed since the last call, in order.
    virtual std::vector<std::string> take_button_presses()
    {
        return {};
    }
};

using ViewerFactory = std::function<std::unique_ptr<Viewer>(ViewerSources)>;

} // namespace video
