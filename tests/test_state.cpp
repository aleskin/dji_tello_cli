#include "tello/state.hpp"
#include "tello/state_listener.hpp"
#include "tello/udp_socket.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace tello {
namespace {

// A packet as the Tello EDU sends it (mission-pad fields first).
constexpr const char* kPacket =
    "mid:-1;x:0;y:0;z:0;mpry:0,0,0;pitch:1;roll:-2;yaw:45;vgx:0;vgy:0;vgz:0;templ:60;temph:63;"
    "tof:95;h:80;bat:87;baro:120.45;time:12;agx:-3.00;agy:5.00;agz:-998.00;\r\n";

TEST(StateTest, ParsesRealPacket) {
    const auto s = parse_state(kPacket);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->pitch, 1);
    EXPECT_EQ(s->roll, -2);
    EXPECT_EQ(s->yaw, 45);
    EXPECT_EQ(s->templ, 60);
    EXPECT_EQ(s->temph, 63);
    EXPECT_EQ(s->tof, 95);
    EXPECT_EQ(s->h, 80);
    EXPECT_EQ(s->bat, 87);
    EXPECT_DOUBLE_EQ(s->baro, 120.45);
    EXPECT_EQ(s->time, 12);
    EXPECT_DOUBLE_EQ(s->agz, -998.0);
}

TEST(StateTest, ParsesBasicTelloPacketWithoutMissionPadFields) {
    const auto s = parse_state("pitch:0;roll:0;yaw:0;bat:55;h:0;\r\n");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->bat, 55);
}

TEST(StateTest, LastFieldWithoutSemicolon) {
    const auto s = parse_state("bat:40;h:120\r\n");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->bat, 40);
    EXPECT_EQ(s->h, 120);
}

TEST(StateTest, RejectsGarbage) {
    EXPECT_FALSE(parse_state("").has_value());
    EXPECT_FALSE(parse_state("ok").has_value());
    EXPECT_FALSE(parse_state("mid:-1;x:0;").has_value());  // no known key
    EXPECT_FALSE(parse_state("bat:full;").has_value());    // known key, bad value
}

TEST(StateTest, FormatRoundTrip) {
    DroneState s;
    s.yaw = -90;
    s.h = 150;
    s.bat = 42;
    s.baro = 1.5;
    s.agz = -1000.0;
    const auto parsed = parse_state(format_state(s));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, s);
}

// Polls until the listener has a packet satisfying `pred` or two seconds pass.
template <typename Pred>
std::optional<StateSnapshot> wait_for(const StateListener& listener, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto snap = listener.latest(); snap && pred(snap->state)) {
            return snap;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

TEST(StateListenerTest, KeepsLatestValidPacket) {
    StateListener listener("127.0.0.1", 0);
    ASSERT_NE(listener.local_port(), 0);
    EXPECT_FALSE(listener.latest().has_value());

    UdpSocket drone("127.0.0.1", 0);
    const auto before = std::chrono::steady_clock::now();
    drone.send_to(kPacket, "127.0.0.1", listener.local_port());
    const auto first = wait_for(listener, [](const DroneState& s) { return s.bat == 87; });
    ASSERT_TRUE(first.has_value());
    EXPECT_GE(first->received, before);

    drone.send_to("garbage", "127.0.0.1", listener.local_port());
    drone.send_to("bat:12;h:30;\r\n", "127.0.0.1", listener.local_port());
    const auto second = wait_for(listener, [](const DroneState& s) { return s.bat == 12; });
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->state.h, 30);
}

}  // namespace
}  // namespace tello
