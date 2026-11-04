#include "cli/figure_plan.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <numbers>
#include <ranges>
#include <sstream>

namespace cli
{
namespace
{

constexpr double kDegree = std::numbers::pi / 180.0;
constexpr int kMinMoveCm = 20;  // go: shorter on every axis is refused
constexpr int kMaxMoveCm = 500; // go: longer on an axis is refused
constexpr int kTowerFromCm = 100;
constexpr int kTowerToCm = 500;
constexpr int kTowerStepCm = 50;
constexpr int kPullDistanceCm = 300;
constexpr int kMaxPullDistanceCm = 1000;
constexpr int kBounceTimes = 3;
constexpr int kMaxBounceTimes = 10;
constexpr int kBounceAmplitudeCm = 100;
constexpr int kBounceLowCm = 100;
constexpr int kFlipHeightCm = 150;
constexpr int kMaxFlips = 8;
constexpr int kMinFlipBattery = 50; // the Tello refuses flips below
constexpr std::chrono::milliseconds kFlipPause{1000};
constexpr std::chrono::milliseconds kPullHold{2000}; // a still shot at both ends of a video move

// Rough Tello timing for the estimate.
constexpr double kTakeoffSeconds = 6.0;
constexpr double kLandSeconds = 5.0;
constexpr double kCommandSeconds = 1.0; // reply and settling of any command
constexpr double kTurnDegPerSecond = 90.0;
constexpr double kFlipSeconds = 3.0;
constexpr double kFullBatteryMinutes = 13.0; // hover time of a full battery

std::string str(double value)
{
    return std::to_string(std::lround(value));
}

// "cw 45" / "ccw 30", empty under one degree.
std::string turn(double degrees)
{
    auto whole = std::lround(degrees);
    whole = ((whole % 360) + 360) % 360;
    if (whole > 180)
    {
        whole -= 360;
    }
    if (whole == 0)
    {
        return {};
    }
    return (whole > 0 ? "cw " : "ccw ") + std::to_string(std::abs(whole));
}

// Numbers fill the positions in order over the defaults, words may stand anywhere.
class Parsed
{
public:
    Parsed(const FigureArgs& args, const std::string& usage, std::vector<int> defaults,
           const std::vector<std::string_view>& words)
        : numbers_(std::move(defaults))
    {
        std::size_t given = 0;
        for (const auto arg : args)
        {
            if (!arg.empty() && (arg[0] == '-' || (arg[0] >= '0' && arg[0] <= '9')))
            {
                if (given == numbers_.size())
                {
                    throw FigureError("usage: " + usage);
                }
                int value = 0;
                const auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
                if (ec != std::errc() || ptr != arg.data() + arg.size())
                {
                    throw FigureError("not a number: '" + std::string(arg) + "', usage: " + usage);
                }
                numbers_.at(given++) = value;
            }
            else if (std::ranges::find(words, arg) != words.end())
            {
                words_.push_back(arg);
            }
            else
            {
                throw FigureError("usage: " + usage);
            }
        }
    }

    [[nodiscard]] int number(std::size_t i) const
    {
        return numbers_.at(i);
    }

    // The last of options given, else the first option.
    [[nodiscard]] std::string_view pick(const std::vector<std::string_view>& options) const
    {
        for (const auto word : std::views::reverse(words_))
        {
            if (std::ranges::find(options, word) != options.end())
            {
                return word;
            }
        }
        return options.front();
    }

    [[nodiscard]] bool has(std::string_view word) const
    {
        return std::ranges::find(words_, word) != words_.end();
    }

    [[nodiscard]] const std::vector<std::string_view>& words() const
    {
        return words_;
    }

private:
    std::vector<int> numbers_;
    std::vector<std::string_view> words_;
};

FigurePlan new_plan(const std::string& name, const std::string& summary, int speed)
{
    FigurePlan plan;
    plan.name = name;
    plan.summary = summary;
    plan.speed = speed;
    return plan;
}

void check_range(const std::string& name, const std::string& what, int value, int low, int high)
{
    if (value < low || value > high)
    {
        throw FigureError(name + " " + what + " must be " + std::to_string(low) + ".." +
                          std::to_string(high) + ", got " + std::to_string(value));
    }
}

void check_speed(const std::string& name, int speed)
{
    check_range(name, "speed", speed, kMinFigureSpeed, kMaxFigureSpeed);
}

// A circle cut into 360 / step legs.
void check_circle(const std::string& name, int radius, int step)
{
    check_range(name, "radius (cm)", radius, kMinFigureRadiusCm, kMaxFigureRadiusCm);
    check_range(name, "step (degrees)", step, kMinFigureStepDeg, kMaxFigureStepDeg);
    if (360 % step != 0)
    {
        throw FigureError(name + " step must divide 360, got " + std::to_string(step));
    }
    const double leg = 2.0 * radius * std::sin(step * kDegree / 2.0);
    if (leg < kMinFigureLegCm)
    {
        throw FigureError(name + ": " + std::to_string(step) + " degrees of a " +
                          std::to_string(radius) + " cm circle is a " + str(leg) +
                          " cm leg, the drone needs " + std::to_string(kMinFigureLegCm) +
                          ": take a larger step or radius");
    }
}

// A vertical figure: center height +- radius stays between the ground margin and the ceiling.
void check_vertical(const std::string& name, int height, int radius)
{
    if (height - radius < kMinFigureGroundCm)
    {
        throw FigureError(name + ": the lowest point (height - radius) must be at least " +
                          std::to_string(kMinFigureGroundCm) + " cm, got " +
                          std::to_string(height - radius));
    }
    if (height + radius > kMaxFigureHeightCm)
    {
        throw FigureError(name + ": the highest point (height + radius) must be at most " +
                          std::to_string(kMaxFigureHeightCm) + " cm, got " +
                          std::to_string(height + radius));
    }
}

FigurePlan plan_flower(const FigureArgs& args, const std::string& usage)
{
    const std::string name = "flower";
    const Parsed p(args, usage, {kFigureHeightCm, kFigureRadiusCm, kFigureStepDeg, kFigureSpeed},
                   {"in", "out", "cw", "ccw"});
    const int height = p.number(0);
    const int radius = p.number(1);
    const int step = p.number(2);
    const int speed = p.number(3);
    check_range(name, "height (cm)", height, kMinFigureHeightCm, kMaxFigureHeightCm);
    check_circle(name, radius, step);
    check_speed(name, speed);
    const std::string_view camera = p.pick({"in", "out"});
    const double sign = p.pick({"cw", "ccw"}) == "cw" ? 1.0 : -1.0;
    const int count = 360 / step;

    FigurePlan plan =
        new_plan(name,
                 "height " + std::to_string(height) + " cm, radius " + std::to_string(radius) +
                     " cm, " + std::to_string(count) + " photos every " + std::to_string(step) +
                     " degrees, camera " + std::string(camera),
                 speed);
    auto point = [&](int k)
    {
        const double bearing = sign * step * k * kDegree;
        return FigureStep::move(radius * std::cos(bearing), radius * std::sin(bearing), height);
    };
    plan.steps.push_back(FigureStep::move(0, 0, height));
    for (int k = 0; k < count; ++k)
    {
        plan.steps.push_back(point(k));
        plan.steps.push_back(FigureStep::face(sign * step * k + (camera == "in" ? 180.0 : 0.0)));
        plan.steps.push_back(FigureStep::photo());
    }
    plan.steps.push_back(point(count));
    plan.steps.push_back(FigureStep::move(0, 0, height));
    return plan;
}

// Wheel (across the pilot's view) and loop (along it): a vertical circle from the top.
FigurePlan plan_vertical(const FigureArgs& args, const std::string& usage, bool wheel)
{
    const std::string name = wheel ? "wheel" : "loop";
    const std::vector<std::string_view> directions =
        wheel ? std::vector<std::string_view>{"cw", "ccw"}
              : std::vector<std::string_view>{"away", "toward"};
    const std::vector<std::string_view> cameras =
        wheel ? std::vector<std::string_view>{"in", "out"}
              : std::vector<std::string_view>{"in", "out", "side"};
    std::vector<std::string_view> words = directions;
    words.insert(words.end(), cameras.begin(), cameras.end());
    const Parsed p(args, usage, {kFigureHeightCm, kFigureRadiusCm, kFigureStepDeg, kFigureSpeed},
                   words);
    const int height = p.number(0);
    const int radius = p.number(1);
    const int step = p.number(2);
    const int speed = p.number(3);
    check_range(name, "height (cm)", height, kMinFigureHeightCm, kMaxFigureHeightCm);
    check_circle(name, radius, step);
    check_speed(name, speed);
    check_vertical(name, height, radius);
    const std::string_view camera = p.pick(cameras);
    const double sign = p.pick(directions) == directions.front() ? 1.0 : -1.0;
    const int count = 360 / step;
    const double heading = camera == "side" ? 90.0 : camera == "in" ? 180.0 : 0.0;

    FigurePlan plan =
        new_plan(name,
                 "height " + std::to_string(height) + " cm, radius " + std::to_string(radius) +
                     " cm, " + std::to_string(count) + " photos every " + std::to_string(step) +
                     " degrees, camera " + std::string(camera),
                 speed);
    // The wheel swings to the pilot's right first, the loop forward.
    const double axis = wheel ? 90.0 * kDegree : 0.0;
    auto point = [&](int k)
    {
        const double t = sign * step * k * kDegree;
        return FigureStep::move(radius * std::sin(t) * std::cos(axis),
                                radius * std::sin(t) * std::sin(axis),
                                height + radius * std::cos(t));
    };
    plan.steps.push_back(FigureStep::move(0, 0, height));
    for (int k = 0; k < count; ++k)
    {
        plan.steps.push_back(point(k));
        plan.steps.push_back(FigureStep::face(heading));
        plan.steps.push_back(FigureStep::photo());
    }
    plan.steps.push_back(point(count));
    plan.steps.push_back(FigureStep::move(0, 0, height));
    return plan;
}

FigurePlan plan_wheel(const FigureArgs& args, const std::string& usage)
{
    return plan_vertical(args, usage, true);
}

FigurePlan plan_loop(const FigureArgs& args, const std::string& usage)
{
    return plan_vertical(args, usage, false);
}

FigurePlan plan_panorama(const FigureArgs& args, const std::string& usage)
{
    const std::string name = "panorama";
    const Parsed p(args, usage, {kFigureHeightCm, kFigureStepDeg}, {"cw", "ccw"});
    const int height = p.number(0);
    const int step = p.number(1);
    check_range(name, "height (cm)", height, kMinFigureHeightCm, kMaxFigureHeightCm);
    check_range(name, "step (degrees)", step, kMinFigureStepDeg, kMaxFigureStepDeg);
    if (360 % step != 0)
    {
        throw FigureError(name + " step must divide 360, got " + std::to_string(step));
    }
    const double sign = p.pick({"cw", "ccw"}) == "cw" ? 1.0 : -1.0;
    const int count = 360 / step;

    FigurePlan plan = new_plan(name,
                               "height " + std::to_string(height) + " cm, " +
                                   std::to_string(count) + " photos every " + std::to_string(step) +
                                   " degrees " + std::string(p.pick({"cw", "ccw"})),
                               kFigureSpeed);
    plan.steps.push_back(FigureStep::move(0, 0, height));
    for (int k = 0; k < count; ++k)
    {
        plan.steps.push_back(FigureStep::face(sign * step * k));
        plan.steps.push_back(FigureStep::photo());
    }
    return plan;
}

FigurePlan plan_tower(const FigureArgs& args, const std::string& usage)
{
    const std::string name = "tower";
    const Parsed p(args, usage, {kTowerFromCm, kTowerToCm, kTowerStepCm}, {});
    const int from = p.number(0);
    const int to = p.number(1);
    const int step = p.number(2);
    check_range(name, "from (cm)", from, kMinFigureHeightCm, kMaxFigureHeightCm);
    check_range(name, "to (cm)", to, kMinFigureHeightCm, kMaxFigureHeightCm);
    check_range(name, "step (cm)", step, kMinMoveCm, kMaxMoveCm);
    if (from >= to)
    {
        throw FigureError(name + ": from must be below to, got " + std::to_string(from) + " and " +
                          std::to_string(to));
    }
    const int count = (to - from) / step + 1;

    FigurePlan plan = new_plan(name,
                               std::to_string(count) + " photos from " + std::to_string(from) +
                                   " to " + std::to_string(from + (count - 1) * step) +
                                   " cm every " + std::to_string(step) + " cm",
                               kFigureSpeed);
    for (int k = 0; k < count; ++k)
    {
        plan.steps.push_back(FigureStep::move(0, 0, from + k * step));
        plan.steps.push_back(FigureStep::photo());
    }
    return plan;
}

FigurePlan plan_dronie(const FigureArgs& args, const std::string& usage)
{
    const std::string name = "dronie";
    const Parsed p(args, usage, {kPullDistanceCm, kFigureHeightCm, kFigureSpeed}, {});
    const int distance = p.number(0);
    const int height = p.number(1);
    const int speed = p.number(2);
    check_range(name, "distance (cm)", distance, kMinFigureRadiusCm, kMaxPullDistanceCm);
    check_range(name, "height (cm)", height, kMinFigureHeightCm, kMaxFigureHeightCm);
    check_speed(name, speed);

    FigurePlan plan = new_plan(name,
                               "camera on you, away " + std::to_string(distance) +
                                   " cm and up to " + std::to_string(height) + " cm",
                               speed);
    plan.video = true;
    plan.steps.push_back(FigureStep::face(180));
    plan.steps.push_back(FigureStep::wait(kPullHold));
    plan.steps.push_back(FigureStep::move(distance, 0, height));
    plan.steps.push_back(FigureStep::wait(kPullHold));
    plan.steps.push_back(FigureStep::move(0, 0, height));
    return plan;
}

FigurePlan plan_reveal(const FigureArgs& args, const std::string& usage)
{
    const std::string name = "reveal";
    const Parsed p(args, usage, {kPullDistanceCm, kFigureHeightCm, kFigureSpeed}, {});
    const int distance = p.number(0);
    const int height = p.number(1);
    const int speed = p.number(2);
    check_range(name, "distance (cm)", distance, kMinFigureRadiusCm, kMaxPullDistanceCm);
    check_range(name, "height (cm)", height, kMinFigureHeightCm + kMinMoveCm, kMaxFigureHeightCm);
    check_speed(name, speed);

    FigurePlan plan =
        new_plan(name,
                 "camera forward, from " + std::to_string(kMinFigureGroundCm) + " cm forward " +
                     std::to_string(distance) + " cm and up to " + std::to_string(height) + " cm",
                 speed);
    plan.video = true;
    plan.steps.push_back(FigureStep::move(0, 0, kMinFigureGroundCm));
    plan.steps.push_back(FigureStep::wait(kPullHold));
    plan.steps.push_back(FigureStep::move(distance, 0, height));
    plan.steps.push_back(FigureStep::wait(kPullHold));
    plan.steps.push_back(FigureStep::move(0, 0, height));
    return plan;
}

FigurePlan plan_bounce(const FigureArgs& args, const std::string& usage)
{
    const std::string name = "bounce";
    const Parsed p(args, usage, {kBounceTimes, kBounceAmplitudeCm, kFigureSpeed}, {"cw", "ccw"});
    const int times = p.number(0);
    const int amplitude = p.number(1);
    const int speed = p.number(2);
    check_range(name, "times", times, 1, kMaxBounceTimes);
    check_range(name, "amplitude (cm)", amplitude, kMinMoveCm, kMaxMoveCm);
    check_speed(name, speed);
    const bool spin = p.has("cw") || p.has("ccw");
    const double sign = p.pick({"cw", "ccw"}) == "cw" ? 1.0 : -1.0;
    const double turn_per_bounce = 360.0 / times;

    FigurePlan plan =
        new_plan(name,
                 std::to_string(times) + " times between " + std::to_string(kBounceLowCm) +
                     " and " + std::to_string(kBounceLowCm + amplitude) + " cm" +
                     (spin ? ", one turn " + std::string(p.pick({"cw", "ccw"})) : std::string()),
                 speed);
    plan.steps.push_back(FigureStep::move(0, 0, kBounceLowCm));
    for (int i = 0; i < times; ++i)
    {
        plan.steps.push_back(FigureStep::move(0, 0, kBounceLowCm + amplitude));
        if (spin)
        {
            plan.steps.push_back(FigureStep::face(sign * turn_per_bounce * (i + 0.5)));
        }
        plan.steps.push_back(FigureStep::move(0, 0, kBounceLowCm));
        if (spin)
        {
            plan.steps.push_back(FigureStep::face(sign * turn_per_bounce * (i + 1)));
        }
    }
    return plan;
}

FigurePlan plan_flips(const FigureArgs& args, const std::string& usage)
{
    const std::string name = "flips";
    const Parsed p(args, usage, {}, {"l", "r", "f", "b"});
    std::vector<std::string_view> flips = p.words();
    if (flips.empty())
    {
        flips = {"l", "r", "f", "b"};
    }
    check_range(name, "flip count", static_cast<int>(flips.size()), 1, kMaxFlips);

    std::string list;
    for (const auto flip : flips)
    {
        list += (list.empty() ? "" : " ") + std::string(flip);
    }
    FigurePlan plan = new_plan(name,
                               std::to_string(flips.size()) + " flips (" + list + ") at " +
                                   std::to_string(kFlipHeightCm) + " cm",
                               kFigureSpeed);
    plan.min_battery = kMinFlipBattery;
    plan.steps.push_back(FigureStep::move(0, 0, kFlipHeightCm));
    for (const auto flip : flips)
    {
        plan.steps.push_back(FigureStep::wait(kFlipPause));
        plan.steps.push_back(FigureStep::sdk("flip " + std::string(flip)));
    }
    plan.steps.push_back(FigureStep::wait(kFlipPause));
    return plan;
}

} // namespace

FigureStep FigureStep::move(double x_cm, double y_cm, double z_cm)
{
    FigureStep s;
    s.kind = Kind::Move;
    s.x_cm = x_cm;
    s.y_cm = y_cm;
    s.z_cm = z_cm;
    return s;
}

FigureStep FigureStep::face(double heading_deg)
{
    FigureStep s;
    s.kind = Kind::Face;
    s.heading_deg = heading_deg;
    return s;
}

FigureStep FigureStep::photo()
{
    FigureStep s;
    s.kind = Kind::Photo;
    return s;
}

FigureStep FigureStep::sdk(std::string command)
{
    FigureStep s;
    s.kind = Kind::Command;
    s.command = std::move(command);
    return s;
}

FigureStep FigureStep::wait(std::chrono::milliseconds pause)
{
    FigureStep s;
    s.kind = Kind::Pause;
    s.pause = pause;
    return s;
}

int FigurePlan::photos() const
{
    return static_cast<int>(std::ranges::count_if(steps,
                                                  [](const FigureStep& s)
                                                  {
                                                      return s.kind == FigureStep::Kind::Photo;
                                                  }));
}

const std::vector<FigureSpec>& figure_specs()
{
    static const std::vector<FigureSpec> specs = {
        {"flower", "flower [height] [radius] [step] [in|out] [cw|ccw] [speed]",
         "take off, circle the takeoff point at height (200 cm), radius (100 cm), a photo every "
         "step degrees (30), camera in to the center or out; back to the center and land",
         plan_flower},
        {"wheel", "wheel [height] [radius] [step] [cw|ccw] [in|out] [speed]",
         "take off, vertical circle across your view around the point height cm above the "
         "takeoff (from the top, cw: right first), photos every step degrees, camera in to you "
         "or out; back to the center and land",
         plan_wheel},
        {"loop", "loop [height] [radius] [step] [away|toward] [in|out|side] [speed]",
         "like wheel, but the circle goes away from you and back (from the top: away first); "
         "camera in (to you), out (forward) or side (right)",
         plan_loop},
        {"panorama", "panorama [height] [step] [cw|ccw]",
         "take off, climb to height (200 cm) and turn on the spot, a photo every step degrees "
         "(30) all around; land",
         plan_panorama},
        {"tower", "tower [from] [to] [step]",
         "take off and climb straight up from (100 cm) to (500 cm), a photo every step (50 cm), "
         "camera forward; land",
         plan_tower},
        {"dronie", "dronie [distance] [height] [speed]",
         "take off, turn the camera on you and fly away (300 cm) and up (200 cm) in one line "
         "with video; back to the center and land",
         plan_dronie},
        {"reveal", "reveal [distance] [height] [speed]",
         "take off, sink low (50 cm), then fly forward (300 cm) and up (200 cm) with video, "
         "camera forward: the view opens up; back to the center and land",
         plan_reveal},
        {"bounce", "bounce [times] [amplitude] [cw|ccw] [speed]",
         "take off, go up and down times (3) between 100 cm and 100 + amplitude (100 cm); with "
         "cw or ccw it also makes one full turn on the way; land",
         plan_bounce},
        {"flips", "flips [l|r|f|b ...]",
         "take off, climb to 150 cm and flip in the given directions (all four), a pause between; "
         "needs 50 % battery; land",
         plan_flips},
    };
    return specs;
}

std::vector<std::string> step_commands(const drone::Position& now, const FigureStep& step,
                                       int speed)
{
    switch (step.kind)
    {
    case FigureStep::Kind::Move:
    {
        const double h = now.heading_deg * kDegree;
        const double dx = step.x_cm - now.x_cm;
        const double dy = step.y_cm - now.y_cm;
        const double forward = dx * std::cos(h) + dy * std::sin(h);
        const double left = dx * std::sin(h) - dy * std::cos(h);
        const double up = step.z_cm - now.z_cm;
        const double longest = std::max({std::abs(forward), std::abs(left), std::abs(up)});
        if (std::lround(longest) < kMinMoveCm)
        {
            return {}; // closer than the drone can step
        }
        const int parts = static_cast<int>(std::ceil(longest / kMaxMoveCm));
        std::vector<std::string> out;
        for (int i = 1; i <= parts; ++i)
        {
            // Rounded cumulatively, so the parts add up to the whole move.
            auto part = [i, parts](double whole)
            {
                return std::to_string(std::lround(whole * i / parts) -
                                      std::lround(whole * (i - 1) / parts));
            };
            out.push_back("go " + part(forward) + ' ' + part(left) + ' ' + part(up) + ' ' +
                          std::to_string(speed));
        }
        return out;
    }
    case FigureStep::Kind::Face:
    {
        std::string t = turn(step.heading_deg - now.heading_deg);
        if (t.empty())
        {
            return {};
        }
        return {t};
    }
    case FigureStep::Kind::Command:
        return {step.command};
    case FigureStep::Kind::Photo:
    case FigureStep::Kind::Pause:
        break;
    }
    return {};
}

void apply_command(drone::PositionSource& source, std::string_view command)
{
    std::istringstream in{std::string(command)};
    std::string name;
    in >> name;
    int a = 0;
    int b = 0;
    int c = 0;
    if (name == "takeoff")
    {
        source.on_takeoff();
    }
    else if (name == "land")
    {
        source.on_land();
    }
    else if (name == "go" && in >> a >> b >> c)
    {
        source.on_go(a, b, c);
    }
    else if ((name == "cw" || name == "ccw") && in >> a)
    {
        source.on_rotate(name == "cw" ? a : -a);
    }
    else if (name == "flip")
    {
        source.on_flip();
    }
    else if (in >> a)
    {
        static const std::array<std::pair<std::string_view, drone::Direction>, 6> moves = {{
            {"up", drone::Direction::Up},
            {"down", drone::Direction::Down},
            {"left", drone::Direction::Left},
            {"right", drone::Direction::Right},
            {"forward", drone::Direction::Forward},
            {"back", drone::Direction::Back},
        }};
        for (const auto& [word, direction] : moves)
        {
            if (name == word)
            {
                source.on_move(direction, a);
            }
        }
    }
}

FigureEstimate estimate(const FigurePlan& plan, drone::PositionSource& source)
{
    FigureEstimate e;
    double seconds = kTakeoffSeconds + kLandSeconds;
    int photo = 0;
    auto run = [&](const std::string& command)
    {
        const auto before = source.position();
        apply_command(source, command);
        const auto after = source.position();
        e.lines.push_back(command);
        ++e.commands;
        const bool ground = command == "takeoff" || command == "land";
        const double flown = ground ? 0.0
                                    : std::hypot(after.x_cm - before.x_cm, after.y_cm - before.y_cm,
                                                 after.z_cm - before.z_cm);
        e.distance_cm += flown;
        e.highest_cm = std::max(e.highest_cm, after.z_cm);
        if (command.starts_with("cw ") || command.starts_with("ccw "))
        {
            seconds += kCommandSeconds +
                       std::stod(command.substr(command.find(' ') + 1)) / kTurnDegPerSecond;
        }
        else if (command.starts_with("flip"))
        {
            seconds += kFlipSeconds;
        }
        else if (!ground)
        {
            seconds += kCommandSeconds + flown / plan.speed;
        }
    };
    run("takeoff");
    for (const auto& step : plan.steps)
    {
        if (step.kind == FigureStep::Kind::Photo)
        {
            std::string file = std::to_string(++photo);
            file.insert(0, 2 - std::min<std::size_t>(file.size(), 2), '0');
            e.lines.push_back("photo " + file);
            seconds += std::chrono::duration<double>(kPhotoSettle).count();
            continue;
        }
        if (step.kind == FigureStep::Kind::Pause)
        {
            e.lines.push_back("wait " + std::to_string(step.pause.count()) + " ms");
            seconds += std::chrono::duration<double>(step.pause).count();
            continue;
        }
        for (const auto& command : step_commands(source.position(), step, plan.speed))
        {
            run(command);
        }
    }
    run("land");
    e.duration = std::chrono::seconds(std::lround(seconds));
    e.battery_percent = static_cast<int>(std::ceil(seconds / 60.0 / kFullBatteryMinutes * 100.0));
    return e;
}

} // namespace cli
