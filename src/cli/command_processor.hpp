#pragma once

#include "drone/drone_base.hpp"
#include "tello/state_listener.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace cli {

enum class Category { System, Flight, Movement, Telemetry };

struct CommandInfo {
    std::string name;
    std::vector<std::string> aliases;
    Category category;
    std::string usage;  // "forward <cm>"
    std::string help;
    std::optional<drone::Capability> needs;  // hidden from help when the model lacks it
};

// Where the CLI talks to, shown by "info".
struct SessionInfo {
    std::string host;
    std::uint16_t command_port = 0;
    std::uint16_t state_port = 0;  // 0 = telemetry off
};

// Interactive command layer. Knows only DroneBase: which model sits behind it decides what works.
class CommandProcessor {
public:
    using Sleep = std::function<void(std::chrono::milliseconds)>;

    CommandProcessor(drone::DroneBase& drone, std::ostream& out, SessionInfo session,
                     const tello::StateListener* telemetry = nullptr, Sleep sleep = {});

    // Runs "cmd; cmd; ..." in order, stops at the first failure. False once "exit" ran.
    bool execute_line(std::string_view line);

    // Command names (with aliases) this model supports and that start with prefix.
    [[nodiscard]] std::vector<std::string> completions(std::string_view prefix) const;

    [[nodiscard]] const std::vector<CommandInfo>& commands() const { return commands_; }

private:
    using Args = std::vector<std::string_view>;
    struct Entry {
        CommandInfo info;
        std::function<void(const Args&)> run;
    };

    // True on success, false when the chain must stop.
    bool execute(std::string_view command);
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
    std::vector<Entry> entries_;
    std::vector<CommandInfo> commands_;
    bool exit_requested_ = false;
};

[[nodiscard]] std::string_view to_string(Category c);

}  // namespace cli
