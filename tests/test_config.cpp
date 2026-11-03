// ~/.config/tello/config parsing: values, defaults, warnings.

#include "cli/config.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <sstream>
#include <string>

namespace
{

cli::LoadedConfig parse(const std::string& text)
{
    std::istringstream in(text);
    return cli::parse_config(in, "config");
}

TEST(ConfigTest, EmptyKeepsDefaults)
{
    const auto c = parse("# nothing here\n\n");
    EXPECT_TRUE(c.warnings.empty());
    EXPECT_FALSE(c.config.host);
    EXPECT_FALSE(c.config.port);
    EXPECT_FALSE(c.config.keyboard_speed);
    EXPECT_EQ(c.config.keys.forward, "w");
}

TEST(ConfigTest, ReadsEveryKey)
{
    const auto c = parse("host = 127.0.0.1\n"
                         "port=18889\n"
                         "state_port = 0   # telemetry off\n"
                         "video_port = 18111\n"
                         "model = tello-edu\n"
                         "warn_battery = 30\n"
                         "land_battery = 20\n"
                         "kbspeed = 60\n"
                         "key_forward = I\n"
                         "key_back = k\n"
                         "key_left = j\n"
                         "key_right = l\n"
                         "key_up = page up\n"
                         "key_down = page down\n"
                         "key_turn_left = u\n"
                         "key_turn_right = o\n");
    EXPECT_TRUE(c.warnings.empty());
    const auto& k = c.config;
    EXPECT_EQ(k.host, "127.0.0.1");
    EXPECT_EQ(k.port, 18889);
    EXPECT_EQ(k.state_port, 0);
    EXPECT_EQ(k.video_port, 18111);
    EXPECT_EQ(k.model, "tello-edu");
    EXPECT_EQ(k.warn_battery, 30);
    EXPECT_EQ(k.land_battery, 20);
    EXPECT_EQ(k.keyboard_speed, 60);
    EXPECT_EQ(k.keys.forward, "i");
    EXPECT_EQ(k.keys.back, "k");
    EXPECT_EQ(k.keys.left, "j");
    EXPECT_EQ(k.keys.right, "l");
    EXPECT_EQ(k.keys.up, "page up");
    EXPECT_EQ(k.keys.down, "page down");
    EXPECT_EQ(k.keys.turn_left, "u");
    EXPECT_EQ(k.keys.turn_right, "o");
}

TEST(ConfigTest, BadLinesWarnAndAreSkipped)
{
    const auto c = parse("hots = 1.2.3.4\n"
                         "port = 70000\n"
                         "kbspeed = 5\n"
                         "warn_battery = abc\n"
                         "just text\n"
                         "model =\n"
                         "host = 127.0.0.1\n");
    ASSERT_EQ(c.warnings.size(), 6U);
    EXPECT_EQ(c.warnings[0], "config:1: hots: unknown key");
    EXPECT_EQ(c.warnings[1], "config:2: port: must be 1..65535, got '70000'");
    EXPECT_EQ(c.warnings[2], "config:3: kbspeed: must be 10..100, got '5'");
    EXPECT_EQ(c.warnings[3], "config:4: warn_battery: must be 0..100, got 'abc'");
    EXPECT_EQ(c.warnings[4], "config:5: expected 'key = value', got 'just text'");
    EXPECT_EQ(c.warnings[5], "config:6: no value for 'model'");
    EXPECT_FALSE(c.config.port);
    EXPECT_EQ(c.config.host, "127.0.0.1");
}

TEST(ConfigTest, LandThresholdMustBeBelowWarning)
{
    const auto c = parse("warn_battery = 10\nland_battery = 15\n");
    ASSERT_EQ(c.warnings.size(), 1U);
    EXPECT_EQ(c.warnings[0], "config: land_battery 15 is not below warn_battery 10");
}

TEST(ConfigTest, MissingFileIsEmpty)
{
    const auto c = cli::load_config("/nonexistent/tello/config");
    EXPECT_TRUE(c.warnings.empty());
    EXPECT_FALSE(c.config.host);
}

TEST(ConfigTest, DirFollowsXdg)
{
    const char* old = std::getenv("XDG_CONFIG_HOME");
    const std::string saved = old ? old : "";
    ::setenv("XDG_CONFIG_HOME", "/tmp/xdg", 1);
    EXPECT_EQ(cli::config_dir(), "/tmp/xdg/tello");
    ::unsetenv("XDG_CONFIG_HOME");
    EXPECT_EQ(cli::config_dir().filename(), "tello");
    EXPECT_EQ(cli::config_dir().parent_path().filename(), ".config");
    if (old)
    {
        ::setenv("XDG_CONFIG_HOME", saved.c_str(), 1);
    }
}

} // namespace
