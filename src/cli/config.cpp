#include "cli/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

namespace cli
{
namespace
{

std::string_view trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

int number(std::string_view value, int low, int high)
{
    int n = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
    if (ec != std::errc() || ptr != value.data() + value.size() || n < low || n > high)
    {
        throw std::invalid_argument("must be " + std::to_string(low) + ".." + std::to_string(high) +
                                    ", got '" + std::string(value) + "'");
    }
    return n;
}

std::string key_name(std::string_view value)
{
    std::string out(value);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
    return out;
}

void apply(Config& c, std::string_view key, std::string_view value)
{
    const std::map<std::string_view, std::string KeyLayout::*> keys = {
        {"key_forward", &KeyLayout::forward},
        {"key_back", &KeyLayout::back},
        {"key_left", &KeyLayout::left},
        {"key_right", &KeyLayout::right},
        {"key_up", &KeyLayout::up},
        {"key_down", &KeyLayout::down},
        {"key_turn_left", &KeyLayout::turn_left},
        {"key_turn_right", &KeyLayout::turn_right},
    };
    if (key == "host")
    {
        c.host = std::string(value);
    }
    else if (key == "port")
    {
        c.port = static_cast<std::uint16_t>(number(value, 1, 65535));
    }
    else if (key == "state_port")
    {
        c.state_port = static_cast<std::uint16_t>(number(value, 0, 65535));
    }
    else if (key == "video_port")
    {
        c.video_port = static_cast<std::uint16_t>(number(value, 1, 65535));
    }
    else if (key == "model")
    {
        c.model = std::string(value);
    }
    else if (key == "warn_battery")
    {
        c.warn_battery = number(value, 0, 100);
    }
    else if (key == "land_battery")
    {
        c.land_battery = number(value, 0, 100);
    }
    else if (key == "kbspeed")
    {
        c.keyboard_speed = number(value, 10, 100);
    }
    else if (key == "js_deadzone")
    {
        c.joystick_deadzone = number(value, 0, 50);
    }
    else if (key == "log_dir")
    {
        c.log_dir = std::string(value);
    }
    else if (key == "log_keep")
    {
        c.log_keep = number(value, 1, 1000);
    }
    else if (const auto it = keys.find(key); it != keys.end())
    {
        c.keys.*(it->second) = key_name(value);
    }
    else
    {
        throw std::invalid_argument("unknown key");
    }
}

} // namespace

LoadedConfig parse_config(std::istream& in, std::string_view name)
{
    LoadedConfig out;
    std::string line;
    for (int n = 1; std::getline(in, line); ++n)
    {
        const auto text = trim(std::string_view(line).substr(0, line.find('#')));
        if (text.empty())
        {
            continue;
        }
        const std::string where = std::string(name) + ':' + std::to_string(n) + ": ";
        const auto eq = text.find('=');
        if (eq == std::string_view::npos)
        {
            out.warnings.push_back(where + "expected 'key = value', got '" + std::string(text) +
                                   "'");
            continue;
        }
        const auto key = trim(text.substr(0, eq));
        const auto value = trim(text.substr(eq + 1));
        if (value.empty())
        {
            out.warnings.push_back(where + "no value for '" + std::string(key) + "'");
            continue;
        }
        try
        {
            apply(out.config, key, value);
        }
        catch (const std::invalid_argument& e)
        {
            out.warnings.push_back(where + std::string(key) + ": " + e.what());
        }
    }
    const auto& c = out.config;
    if (c.warn_battery && c.land_battery && *c.land_battery >= *c.warn_battery)
    {
        out.warnings.push_back(std::string(name) + ": land_battery " +
                               std::to_string(*c.land_battery) + " is not below warn_battery " +
                               std::to_string(*c.warn_battery));
    }
    return out;
}

LoadedConfig load_config(const std::filesystem::path& file)
{
    std::ifstream in(file);
    if (!in)
    {
        return {};
    }
    return parse_config(in, file.string());
}

std::filesystem::path config_dir()
{
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0')
    {
        return std::filesystem::path(xdg) / "tello";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : ".") / ".config" / "tello";
}

} // namespace cli
