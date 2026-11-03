// Aliases: parameters, chains, persistence, refusals.

#include "cli/command_processor.hpp"
#include "mock_transport.hpp"
#include "tello/tello_family.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using tello::test::MockTransport;

class AliasCli : public ::testing::Test
{
protected:
    AliasCli()
    {
        std::filesystem::remove_all(dir);
    }
    ~AliasCli() override
    {
        std::filesystem::remove_all(dir);
    }
    AliasCli(const AliasCli&) = delete;
    AliasCli& operator=(const AliasCli&) = delete;
    AliasCli(AliasCli&&) = delete;
    AliasCli& operator=(AliasCli&&) = delete;

    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("tello_alias_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
         ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::shared_ptr<MockTransport> mock = std::make_shared<MockTransport>();
    tello::TelloClassic drone{mock};
    std::ostringstream out;
    bool interrupted = false;
    cli::CommandProcessor cli{drone,
                              out,
                              {"127.0.0.1", 8889, 0, {}, {}},
                              nullptr,
                              [](std::chrono::milliseconds)
                              {
                              },
                              [this]
                              {
                                  return interrupted;
                              }};
};

TEST_F(AliasCli, ParametersAreSubstituted)
{
    cli.execute_line("alias hop up $1; down $1");
    EXPECT_EQ(cli.aliases().at("hop"), "up $1; down $1");
    cli.execute_line("takeoff; hop 50; land");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "up 50", "down 50", "land"}));
}

TEST_F(AliasCli, AllArgumentsAndTwoParameters)
{
    cli.execute_line("alias box forward $1; cw 90; forward $2");
    cli.execute_line("alias fw forward $*");
    cli.execute_line("box 100 200; fw 30");
    EXPECT_EQ(mock->requests,
              (std::vector<std::string>{"forward 100", "cw 90", "forward 200", "forward 30"}));
}

TEST_F(AliasCli, MissingOrExtraArgumentSendsNothing)
{
    cli.execute_line("alias hop up $1; down $1");
    cli.execute_line("hop; land");
    EXPECT_NE(out.str().find("needs argument $1"), std::string::npos);
    cli.execute_line("hop 50 60; land");
    EXPECT_NE(out.str().find("takes 1 argument(s), got 2"), std::string::npos);
    EXPECT_TRUE(mock->requests.empty());
}

TEST_F(AliasCli, FailureInsideAliasStopsTheOuterChain)
{
    cli.execute_line("alias hop up $1; down $1");
    mock->reply("error Not joystick");
    cli.execute_line("hop 50; land");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"up 50"}));
}

TEST_F(AliasCli, AliasCanCallAliasButNotLoopForever)
{
    cli.execute_line("alias a1 up $1");
    cli.execute_line("alias a2 a1 $1; a1 $1");
    cli.execute_line("a2 30");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"up 30", "up 30"}));

    cli.execute_line("alias loop loop");
    cli.execute_line("loop; land");
    EXPECT_NE(out.str().find("too deep"), std::string::npos);
    EXPECT_EQ(mock->requests.size(), 2U);
}

TEST_F(AliasCli, ListShowAndUnalias)
{
    cli.execute_line("aliases");
    EXPECT_NE(out.str().find("no aliases"), std::string::npos);
    cli.execute_line("alias hop up $1; down $1");
    out.str("");
    cli.execute_line("alias hop");
    EXPECT_EQ(out.str(), "alias hop up $1; down $1\n");
    out.str("");
    cli.execute_line("aliases");
    EXPECT_EQ(out.str(), "alias hop up $1; down $1\n");
    cli.execute_line("unalias hop");
    EXPECT_TRUE(cli.aliases().empty());
    out.str("");
    cli.execute_line("unalias hop");
    EXPECT_NE(out.str().find("no alias 'hop'"), std::string::npos);
}

TEST_F(AliasCli, RefusesCommandNamesAndBadNames)
{
    cli.execute_line("alias land emergency");
    EXPECT_NE(out.str().find("'land' is a command"), std::string::npos);
    cli.execute_line("alias ls? up 20");
    EXPECT_NE(out.str().find("alias name"), std::string::npos);
    EXPECT_TRUE(cli.aliases().empty());
}

TEST_F(AliasCli, AliasNamesAreCompleted)
{
    cli.execute_line("alias hop up $1");
    const auto names = cli.completions("h");
    EXPECT_NE(std::ranges::find(names, "hop"), names.end());
    EXPECT_NE(std::ranges::find(names, "help"), names.end());
}

TEST_F(AliasCli, SavedAndLoadedFromFile)
{
    const auto file = dir / "tello" / "aliases";
    cli.load_aliases(file); // missing: fine
    cli.execute_line("alias hop up $1; down $1");
    cli.execute_line("alias spin cw 360");
    {
        std::ifstream in(file);
        std::stringstream text;
        text << in.rdbuf();
        EXPECT_EQ(text.str(), "hop up $1; down $1\nspin cw 360\n");
    }
    {
        std::ofstream add(file, std::ios::app);
        add << "# comment\n\nland emergency\nbroken\n";
    }
    std::ostringstream out2;
    cli::CommandProcessor other{drone, out2, {"127.0.0.1", 8889, 0, {}, {}}};
    other.load_aliases(file);
    EXPECT_EQ(other.aliases().size(), 2U);
    EXPECT_EQ(other.aliases().at("spin"), "cw 360");
    EXPECT_NE(out2.str().find("aliases:5: bad alias line skipped"), std::string::npos);
    EXPECT_NE(out2.str().find("aliases:6: bad alias line skipped"), std::string::npos);
}

TEST_F(AliasCli, AliasesWorkInMissions)
{
    cli.execute_line("alias hop up $1; down $1");
    std::istringstream mission("takeoff\nhop 40 # bounce\nland\n");
    EXPECT_TRUE(cli.run_mission(mission, "m"));
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff", "up 40", "down 40", "land"}));
}

} // namespace
