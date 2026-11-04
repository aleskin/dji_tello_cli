#pragma once

#include "drone/position.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cli
{

// flower/wheel/loop/... defaults and limits (cm, degrees, cm/s).
constexpr int kFigureHeightCm = 200;
constexpr int kFigureRadiusCm = 100;
constexpr int kFigureStepDeg = 30;
constexpr int kFigureSpeed = 30;
constexpr int kMinFigureSpeed = 10;
constexpr int kMaxFigureSpeed = 100;
constexpr int kMinFigureRadiusCm = 50;
constexpr int kMaxFigureRadiusCm = 500;
constexpr int kMinFigureHeightCm = 50;
constexpr int kMaxFigureHeightCm = 1000; // the highest point of a figure
constexpr int kMinFigureGroundCm = 50;   // the lowest point of a vertical figure
constexpr int kMinFigureLegCm = 30;      // one step of a circle; go needs 20 cm on an axis
constexpr int kMinFigureStepDeg = 10;
constexpr int kMaxFigureStepDeg = 120;
// Hover before a photo: a steady, fresh picture.
constexpr std::chrono::milliseconds kPhotoSettle{1000};

// Bad figure arguments: printed as is, nothing was sent.
class FigureError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// One step of a figure. Points are in the takeoff frame (x forward, y right, z up, cm),
// headings clockwise from the takeoff heading.
struct FigureStep
{
    enum class Kind
    {
        Move,    // straight to (x, y, z), the heading stays
        Face,    // turn to heading
        Photo,   // hover kPhotoSettle, save a frame
        Command, // an SDK command as is: "flip l"
        Pause    // hover for pause
    };
    Kind kind = Kind::Move;
    double x_cm = 0.0;
    double y_cm = 0.0;
    double z_cm = 0.0;
    double heading_deg = 0.0;
    std::string command;
    std::chrono::milliseconds pause{0};

    static FigureStep move(double x_cm, double y_cm, double z_cm);
    static FigureStep face(double heading_deg);
    static FigureStep photo();
    static FigureStep sdk(std::string command);
    static FigureStep wait(std::chrono::milliseconds pause);
};

// What a figure flies between "takeoff" and "land".
struct FigurePlan
{
    std::string name;
    std::string summary; // "height 200 cm, radius 100 cm, 12 photos every 30 degrees, camera in"
    int speed = kFigureSpeed;
    int min_battery = 0; // percent needed before the takeoff
    bool video = false;  // record the camera from the takeoff to the landing
    int movie_fps = 0;   // > 0: the photos are put together into an MP4 after the landing
    std::vector<FigureStep> steps;

    [[nodiscard]] int photos() const;
};

using FigureArgs = std::vector<std::string_view>;
using FigurePlanner = FigurePlan (*)(const FigureArgs& args, const std::string& usage);

// One figure command of the CLI.
struct FigureSpec
{
    std::string name;
    std::string usage;
    std::string help;
    FigurePlanner plan;
};

// Every figure the CLI offers, in help order.
[[nodiscard]] const std::vector<FigureSpec>& figure_specs();

// SDK commands that take the drone from `now` through `step`: "go ...", "cw 30", "flip l".
// Empty when there is nothing to fly (a move under 20 cm, a turn under a degree, a photo).
[[nodiscard]] std::vector<std::string> step_commands(const drone::Position& now,
                                                     const FigureStep& step, int speed);

// SDK commands from `from` to `to`: a go in from's frame, then a turn to to's heading.
// A manual flight in a recorded route; empty when the drone stayed put.
[[nodiscard]] std::vector<std::string> route_commands(const drone::Position& from,
                                                      const drone::Position& to, int speed);

// Counts the position of a completed SDK command into source: go, cw/ccw, flip, up/down...
void apply_command(drone::PositionSource& source, std::string_view command);

// The whole flight on paper, from "takeoff" to "land": what dry run prints.
struct FigureEstimate
{
    std::vector<std::string> lines; // commands, and "photo 01" where a photo is taken
    int commands = 0;
    std::chrono::seconds duration{0};
    double distance_cm = 0.0;
    double highest_cm = 0.0;
    int battery_percent = 0;
};

// source: a fresh position source of the drone's model, used as the paper drone.
[[nodiscard]] FigureEstimate estimate(const FigurePlan& plan, drone::PositionSource& source);

} // namespace cli
