#pragma once

#include "drone/drone_base.hpp"
#include "tello/state_listener.hpp"
#include "video/video_stream.hpp"
#include "video/viewer.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <istream>
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

constexpr int kDefaultKeyboardSpeed = 40;
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
    void keyboard_mode();
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
    int keyboard_speed_ = kDefaultKeyboardSpeed;
    std::atomic<bool> keyboard_active_{false}; // read by the window thread for the OSD
    std::unique_ptr<video::Viewer> viewer_; // after video_: it reads the stream, so it goes first
};

[[nodiscard]] std::string_view to_string(Category c);

} // namespace cli
