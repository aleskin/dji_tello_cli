#include "mock_transport.hpp"
#include "tello/drone.hpp"

#include <gtest/gtest.h>

namespace tello {
namespace {

using test::MockTransport;

class DroneTest : public ::testing::Test {
protected:
    MockTransport transport;
    DroneTimeouts timeouts{std::chrono::milliseconds(500), std::chrono::milliseconds(100)};
    Drone drone{transport, timeouts};
};

TEST_F(DroneTest, ConnectSendsCommand) {
    drone.connect();
    ASSERT_EQ(transport.requests.size(), 1u);
    EXPECT_EQ(transport.requests[0], "command");
}

TEST_F(DroneTest, TakeoffAndLand) {
    drone.takeoff();
    drone.land();
    EXPECT_EQ(transport.requests, (std::vector<std::string>{"takeoff", "land"}));
    EXPECT_EQ(transport.last_timeout, timeouts.action);
}

TEST_F(DroneTest, ErrorReplyThrowsWithDroneText) {
    transport.reply("error Not joystick");
    try {
        drone.takeoff();
        FAIL() << "expected TelloError";
    } catch (const TelloError& e) {
        EXPECT_STREQ(e.what(), "takeoff: error Not joystick");
    }
}

TEST_F(DroneTest, TimeoutThrows) {
    transport.reply(std::nullopt);
    EXPECT_THROW(drone.land(), TelloError);
}

TEST_F(DroneTest, MoveFormatsCommand) {
    drone.move(Direction::Forward, 100);
    drone.move(Direction::Up, 20);
    drone.move(Direction::Back, 500);
    EXPECT_EQ(transport.requests,
              (std::vector<std::string>{"forward 100", "up 20", "back 500"}));
}

TEST_F(DroneTest, MoveOutOfRangeSendsNothing) {
    EXPECT_THROW(drone.move(Direction::Left, 19), TelloError);
    EXPECT_THROW(drone.move(Direction::Right, 501), TelloError);
    EXPECT_TRUE(transport.requests.empty());
}

TEST_F(DroneTest, Rotate) {
    drone.rotate_cw(90);
    drone.rotate_ccw(360);
    EXPECT_EQ(transport.requests, (std::vector<std::string>{"cw 90", "ccw 360"}));
    EXPECT_THROW(drone.rotate_cw(0), TelloError);
    EXPECT_THROW(drone.rotate_ccw(361), TelloError);
    EXPECT_EQ(transport.requests.size(), 2u);
}

TEST_F(DroneTest, FlipAndSpeed) {
    drone.flip(FlipDirection::Back);
    drone.set_speed(50);
    EXPECT_EQ(transport.requests, (std::vector<std::string>{"flip b", "speed 50"}));
    EXPECT_THROW(drone.set_speed(9), TelloError);
    EXPECT_THROW(drone.set_speed(101), TelloError);
}

TEST_F(DroneTest, GoValidatesSpan) {
    drone.go(100, -50, 0, 30);
    EXPECT_EQ(transport.requests.back(), "go 100 -50 0 30");
    EXPECT_THROW(drone.go(10, -10, 19, 30), TelloError);   // all within -20..20
    EXPECT_THROW(drone.go(501, 0, 0, 30), TelloError);
    EXPECT_THROW(drone.go(100, 0, 0, 5), TelloError);
    EXPECT_EQ(transport.requests.size(), 1u);
}

TEST_F(DroneTest, RcIsFireAndForget) {
    drone.rc(0, 50, -20, 100);
    EXPECT_TRUE(transport.requests.empty());
    EXPECT_EQ(transport.sent, (std::vector<std::string>{"rc 0 50 -20 100"}));
    EXPECT_THROW(drone.rc(101, 0, 0, 0), TelloError);
}

TEST_F(DroneTest, QueriesParseNumbersWithUnits) {
    transport.reply("87");
    transport.reply("12dm");
    EXPECT_EQ(drone.battery(), 87);
    EXPECT_EQ(drone.height_cm(), 120);
    EXPECT_EQ(transport.last_timeout, timeouts.query);
}

TEST_F(DroneTest, QueryRejectsGarbageAndErrors) {
    transport.reply("abc");
    EXPECT_THROW((void)drone.battery(), TelloError);
    transport.reply("error");
    EXPECT_THROW((void)drone.temperature(), TelloError);
    transport.reply("unknown command: sn?");
    EXPECT_THROW((void)drone.serial_number(), TelloError);
}

TEST_F(DroneTest, StringQueries) {
    transport.reply("63~65C");
    transport.reply("20");
    EXPECT_EQ(drone.temperature(), "63~65C");
    EXPECT_EQ(drone.sdk_version(), "20");
    EXPECT_EQ(transport.requests, (std::vector<std::string>{"temp?", "sdk?"}));
}

TEST_F(DroneTest, SensorQueries) {
    transport.reply("100.0");
    transport.reply("pitch:1;roll:-2;yaw:30;");
    transport.reply("0.25");
    transport.reply("100mm");
    EXPECT_EQ(drone.speed(), 100);
    EXPECT_EQ(drone.attitude(), "pitch:1;roll:-2;yaw:30;");
    EXPECT_EQ(drone.barometer(), "0.25");
    EXPECT_EQ(drone.tof_distance(), "100mm");
    EXPECT_EQ(transport.requests,
              (std::vector<std::string>{"speed?", "attitude?", "baro?", "tof?"}));
}

TEST_F(DroneTest, RemainingQueriesUseSdkNames) {
    transport.reply("90");
    transport.reply("15s");
    transport.reply("0TQZK1234");
    EXPECT_EQ(drone.wifi_snr(), 90);
    EXPECT_EQ(drone.flight_time(), "15s");
    EXPECT_EQ(drone.serial_number(), "0TQZK1234");
    EXPECT_EQ(transport.requests, (std::vector<std::string>{"wifi?", "time?", "sn?"}));
}

TEST_F(DroneTest, StopAndEmergency) {
    drone.stop();
    drone.emergency();
    EXPECT_EQ(transport.requests, (std::vector<std::string>{"stop", "emergency"}));
}

}  // namespace
}  // namespace tello
