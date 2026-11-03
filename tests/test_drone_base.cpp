// The three layers: DroneBase stubs, the Tello family, model differences, the factory.

#include "drone/drone_base.hpp"
#include "drone/errors.hpp"
#include "drone/factory.hpp"
#include "mock_transport.hpp"
#include "tello/tello_family.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace
{

using drone::Capability;
using drone::DroneBase;
using drone::NotSupported;

// A model that overrides nothing but its name: every command must hit the base stub.
class BareModel final : public DroneBase
{
public:
    [[nodiscard]] std::string family() const override
    {
        return "Test";
    }
    [[nodiscard]] std::string model() const override
    {
        return "Bare";
    }
};

TEST(DroneBaseTest, StubsThrowNotSupportedWithModelName)
{
    BareModel bare;
    EXPECT_TRUE(bare.capabilities().empty());
    EXPECT_FALSE(bare.supports(Capability::Takeoff));
    try
    {
        bare.takeoff();
        FAIL() << "expected NotSupported";
    }
    catch (const NotSupported& e)
    {
        EXPECT_STREQ(e.what(), "Bare: 'takeoff' is not supported");
    }
    EXPECT_THROW(bare.move(drone::Direction::Up, 50), NotSupported);
    EXPECT_THROW((void)bare.battery(), NotSupported);
    EXPECT_THROW((void)bare.serial_number(), NotSupported);
}

TEST(DroneBaseTest, NotSupportedIsADroneError)
{
    BareModel bare;
    EXPECT_THROW(bare.land(), drone::DroneError);
}

class ModelTest : public ::testing::Test
{
protected:
    std::shared_ptr<tello::test::MockTransport> mock =
        std::make_shared<tello::test::MockTransport>();
};

TEST_F(ModelTest, ClassicLacksSdk2CommandsAndSendsNothing)
{
    tello::TelloClassic classic(mock);
    std::unique_ptr<DroneBase> base = std::make_unique<tello::TelloClassic>(mock);
    EXPECT_EQ(classic.family(), "Tello");
    EXPECT_FALSE(classic.supports(Capability::Hover));
    EXPECT_FALSE(classic.supports(Capability::SdkVersion));
    EXPECT_FALSE(classic.supports(Capability::SerialNumber));
    EXPECT_TRUE(classic.supports(Capability::Battery));

    EXPECT_THROW(base->hover(), NotSupported);
    EXPECT_THROW((void)base->sdk_version(), NotSupported);
    EXPECT_THROW((void)base->serial_number(), NotSupported);
    EXPECT_TRUE(mock->requests.empty());

    base->takeoff(); // family command through the base pointer
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"takeoff"}));
}

TEST_F(ModelTest, EduAddsSdk2Commands)
{
    std::unique_ptr<DroneBase> edu = std::make_unique<tello::TelloEdu>(mock);
    EXPECT_TRUE(edu->supports(Capability::Hover));
    EXPECT_TRUE(edu->supports(Capability::SerialNumber));
    EXPECT_TRUE(edu->supports(Capability::Takeoff));
    edu->hover();
    mock->reply("20");
    EXPECT_EQ(edu->sdk_version(), "20");
    EXPECT_EQ(mock->requests, (std::vector<std::string>{"stop", "sdk?"}));
}

TEST_F(ModelTest, EveryCapabilityHasAName)
{
    tello::TelloEdu edu(mock);
    for (const auto c : edu.capabilities())
    {
        EXPECT_NE(drone::to_string(c), "?");
    }
}

TEST(FactoryTest, BuildsKnownModels)
{
    drone::ConnectionSettings settings;
    settings.host = "127.0.0.1"; // nothing is sent on construction, but never the real drone
    settings.port = 9;
    EXPECT_EQ(drone::make_drone(settings)->model(), "Tello (SDK 1.3)");
    settings.model = "tello-edu";
    EXPECT_EQ(drone::make_drone(settings)->model(), "Tello EDU (SDK 2.0)");
    EXPECT_EQ(drone::known_models().size(), 2u);
}

TEST(FactoryTest, UnknownModelThrows)
{
    drone::ConnectionSettings settings;
    settings.host = "127.0.0.1";
    settings.model = "mavic";
    EXPECT_THROW((void)drone::make_drone(settings), drone::DroneError);
}

} // namespace
