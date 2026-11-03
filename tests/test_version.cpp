#include <gtest/gtest.h>

#include "tello/version.hpp"

TEST(Version, ComesFromMakefileVersion)
{
    EXPECT_FALSE(tello::version().empty());
    EXPECT_EQ(tello::version().find('\n'), std::string_view::npos);
}
