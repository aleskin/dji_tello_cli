#pragma once

#include "cli/command_processor.hpp"

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cli
{

// ~/.config/tello/config: "key = value" lines, '#' starts a comment. Unset keys keep defaults;
// command-line flags override what is set here.
struct Config
{
    std::optional<std::string> host;
    std::optional<std::uint16_t> port;
    std::optional<std::uint16_t> state_port;
    std::optional<std::uint16_t> video_port;
    std::optional<std::string> model;
    std::optional<int> warn_battery;
    std::optional<int> land_battery;
    std::optional<int> keyboard_speed;
    KeyLayout keys;
};

struct LoadedConfig
{
    Config config;
    std::vector<std::string> warnings; // "config:3: unknown key 'hots'", bad lines are skipped
};

[[nodiscard]] LoadedConfig parse_config(std::istream& in, std::string_view name);

// A missing file is an empty config.
[[nodiscard]] LoadedConfig load_config(const std::filesystem::path& file);

// $XDG_CONFIG_HOME/tello, or ~/.config/tello: home of "config" and "aliases".
[[nodiscard]] std::filesystem::path config_dir();

} // namespace cli
