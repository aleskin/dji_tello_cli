#pragma once

#include "drone/drone_base.hpp"
#include "tello/state_listener.hpp"

#include <chrono>
#include <functional>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace cli
{

enum class Category
{
    System,
    Flight,
    Movement,
    Mission,
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
    std::uint16_t state_port = 0; // 0 = telemetry off
};

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
};

[[nodiscard]] std::string_view to_string(Category c);

} // namespace cli
