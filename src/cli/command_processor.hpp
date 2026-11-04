#pragma once

#include "drone/drone_base.hpp"
#include "drone/position.hpp"
#include "tello/state_listener.hpp"
#include "video/video_stream.hpp"
#include "video/viewer.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cli
{

enum class Category
{
    System,
    Flight,
    Movement,
    Mission,
    Video,
    Telemetry
};

struct CommandInfo
{
    std::string name;
    std::vector<std::string> aliases;
    Category category;
    std::string usage; // "forward <cm>"
    std::string help;
    std::optional<drone::Capability> needs; // hidden from help when the model lacks it
};

// Where the CLI talks to, shown by "info".
struct SessionInfo
{
    std::string host;
    std::uint16_t command_port = 0;
    std::uint16_t state_port = 0;    // 0 = telemetry off
    std::filesystem::path video_dir; // recordings go here (~/Videos/tello)
    std::filesystem::path photo_dir; // photos go here (~/Pictures/tello)
};

// Keyboard mode bindings: lowercase key names as the window reports them (see video::Viewer).
struct KeyLayout
{
    std::string forward = "w";
    std::string back = "s";
    std::string left = "a";
    std::string right = "d";
    std::string up = "up";
    std::string down = "down";
    std::string turn_left = "left";
    std::string turn_right = "right";
};

// rc a b c d (left/right, forward/back, up/down, yaw) for the keys held down; opposite keys
// cancel out.
struct RcSticks
{
    int left_right = 0;
    int forward_back = 0;
    int up_down = 0;
    int yaw = 0;

    bool operator==(const RcSticks&) const = default;
};
[[nodiscard]] RcSticks sticks_from_keys(const std::set<std::string>& pressed,
                                        const KeyLayout& layout, int speed);

// Gamepad sticks, mode 2: left stick climbs and turns, right stick moves. Inside the dead zone
// (percent of the stick travel) an axis is 0; beyond it the deflection is rescaled to 0..100.
[[nodiscard]] RcSticks sticks_from_gamepad(const video::Gamepad& pad, int deadzone);

// Joystick mode buttons: takeoff on the ground, land in the air, emergency needs both buttons.
struct PadButtons
{
    static constexpr const char* takeoff = "a";
    static constexpr const char* land = "b";
    static constexpr const char* emergency_1 = "back";
    static constexpr const char* emergency_2 = "start";
    static constexpr const char* leave = "guide";
};

constexpr int kDefaultKeyboardSpeed = 40;
constexpr int kOrbitLegs = 12;  // 30 degrees each, camera to the center at every leg end
constexpr int kOrbitSpeed = 30; // cm/s when not given
constexpr int kMinOrbitCm = 50; // a 30-degree leg must stay longer than go's 20 cm
constexpr int kMaxOrbitCm = 500;
constexpr int kMinOrbitSpeed = 10;
constexpr int kMaxOrbitSpeed = 100;
constexpr int kDefaultDeadzone = 10;
// flower/wheel/loop defaults: center height, radius, photo step.
constexpr int kFigureHeightCm = 200;
constexpr int kFigureRadiusCm = 100;
constexpr int kFigureStepDeg = 30;
constexpr int kMinFigureHeightCm = 50;
constexpr int kMaxFigureHeightCm = 1000; // the highest point of a figure
constexpr int kMinFigureGroundCm = 50;   // the lowest point of a wheel or loop
constexpr int kMinFigureLegCm = 30;      // one step of the circle; go needs 20 cm on an axis
// Hover before a photo: a steady, fresh picture.
constexpr std::chrono::milliseconds kPhotoSettle{1000};
constexpr std::chrono::milliseconds kKeyboardPeriod{50}; // rc at 20 Hz

// Interactive command layer. Knows only DroneBase: which model sits behind it decides what works.
class CommandProcessor
{
public:
    using Sleep = std::function<void(std::chrono::milliseconds)>;
    using Interrupted = std::function<bool()>; // true once the user pressed Ctrl+C

    CommandProcessor(drone::DroneBase& drone, std::ostream& out, SessionInfo session,
                     const tello::StateListener* telemetry = nullptr, Sleep sleep = {},
                     Interrupted interrupted = {});

    // Runs "cmd; cmd; ..." in order, stops at the first failure or on Ctrl+C.
    // False once "exit" ran.
    bool execute_line(std::string_view line);

    // Mission: one command line per input line, '#' starts a comment. Stops at the first failure
    // and lands if the drone is in the air. True when every line succeeded.
    bool run_mission(std::istream& in, std::string_view name);

    // Ctrl+C policy: in the air it lands (never quits with a flying drone), on the ground nothing.
    void on_interrupt();

    // After a successful takeoff until a successful land/emergency.
    [[nodiscard]] bool airborne() const
    {
        return airborne_;
    }

    // Command names (with aliases) this model supports and that start with prefix.
    [[nodiscard]] std::vector<std::string> completions(std::string_view prefix) const;

    [[nodiscard]] const std::vector<CommandInfo>& commands() const
    {
        return commands_;
    }

    // How "video show" opens the live window; without a factory the command is refused.
    void set_viewer_factory(video::ViewerFactory factory)
    {
        viewer_factory_ = std::move(factory);
    }

    void set_key_layout(KeyLayout layout)
    {
        key_layout_ = std::move(layout);
    }

    // Caller validates the range (10..100), see "kbspeed".
    void set_keyboard_speed(int speed)
    {
        keyboard_speed_ = speed;
    }

    // Caller validates the range (0..50), see "js_deadzone" in the config.
    void set_joystick_deadzone(int percent)
    {
        joystick_deadzone_ = percent;
    }

    // Aliases are read from this file now and written back after every alias/unalias.
    // A missing file is fine; bad lines are reported and skipped.
    void load_aliases(const std::filesystem::path& file);

    [[nodiscard]] const std::map<std::string, std::string>& aliases() const
    {
        return aliases_;
    }

    // nullptr when the model cannot tell where it is.
    [[nodiscard]] const drone::PositionSource* position() const
    {
        return position_.get();
    }

private:
    using Args = std::vector<std::string_view>;
    struct Entry
    {
        CommandInfo info;
        std::function<void(const Args&)> run;
    };

    enum class Outcome
    {
        Done,
        Failed,
        Interrupted,
        Exit
    };
    Outcome run_chain(std::string_view line);

    // True on success, false when the chain must stop.
    bool execute(std::string_view command);
    bool run_mission_lines(std::istream& in, std::string_view name);
    [[nodiscard]] const Entry* find(std::string_view name) const;
    [[nodiscard]] bool available(const CommandInfo& info) const;

    void add(CommandInfo info, std::function<void(const Args&)> run);
    void register_commands();
    void print_help(const Args& args) const;
    void print_info();
    void print_state() const;
    void video_command(const Args& args);
    void ensure_stream(); // streamon + reader, if not running yet
    void take_photo();
    void show_window();
    void alias_command(std::string_view line);
    void unalias_command(const Args& args);
    void print_aliases() const;
    void save_aliases();
    bool run_alias(const std::string& body, std::string_view name, const Args& args);
    void keyboard_mode();
    void joystick_mode();
    void open_manual_window(std::string_view mode);
    void feed_motion(std::optional<std::chrono::steady_clock::time_point>& last);
    void print_position() const;
    void return_home();
    void face_point(const Args& args, bool away);
    void orbit(const Args& args);
    enum class Figure
    {
        Flower, // horizontal circle around the takeoff point
        Wheel,  // vertical circle across the pilot's view
        Loop    // vertical circle along the pilot's view
    };
    void figure(Figure kind, const Args& args);
    bool fly_to(double x_cm, double y_cm, double z_cm, int speed); // false: the chain failed
    bool face(double heading_deg);
    [[nodiscard]] video::Osd osd() const;

    drone::DroneBase& drone_;
    std::ostream& out_;
    SessionInfo session_;
    const tello::StateListener* telemetry_;
    Sleep sleep_;
    Interrupted interrupted_;
    std::vector<Entry> entries_;
    std::vector<CommandInfo> commands_;
    bool exit_requested_ = false;
    bool airborne_ = false;
    bool in_mission_ = false;
    std::unique_ptr<video::VideoStream> video_; // between "video start" and "video stop"
    video::ViewerFactory viewer_factory_;
    KeyLayout key_layout_;
    std::map<std::string, std::string> aliases_; // name -> "cmd $1; cmd"
    std::filesystem::path aliases_file_;         // empty: aliases are not saved
    int alias_depth_ = 0;                        // aliases calling aliases
    int keyboard_speed_ = kDefaultKeyboardSpeed;
    int joystick_deadzone_ = kDefaultDeadzone;
    std::atomic<const char*> manual_mode_{""}; // "KEYBOARD"/"JOYSTICK", read by the window thread
    std::unique_ptr<drone::PositionSource> position_;
    std::unique_ptr<video::Viewer> viewer_; // after video_: it reads the stream, so it goes first
};

[[nodiscard]] std::string_view to_string(Category c);

} // namespace cli
