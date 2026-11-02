#include "tello/drone.hpp"

#include "tello/errors.hpp"

#include <cctype>
#include <charconv>
#include <cstdlib>

namespace tello {
namespace {

void check_range(std::string_view what, int value, int min, int max) {
    if (value < min || value > max) {
        throw TelloError(std::string(what) + " must be " + std::to_string(min) + ".." +
                         std::to_string(max) + ", got " + std::to_string(value));
    }
}

}  // namespace

std::string_view to_sdk(Direction d) {
    switch (d) {
        case Direction::Up: return "up";
        case Direction::Down: return "down";
        case Direction::Left: return "left";
        case Direction::Right: return "right";
        case Direction::Forward: return "forward";
        case Direction::Back: return "back";
    }
    return "?";
}

char to_sdk(FlipDirection d) {
    switch (d) {
        case FlipDirection::Left: return 'l';
        case FlipDirection::Right: return 'r';
        case FlipDirection::Forward: return 'f';
        case FlipDirection::Back: return 'b';
    }
    return '?';
}

Drone::Drone(ITransport& transport, DroneTimeouts timeouts)
    : transport_(transport), timeouts_(timeouts) {}

void Drone::connect() { expect_ok("command"); }
void Drone::takeoff() { expect_ok("takeoff"); }
void Drone::land() { expect_ok("land"); }
void Drone::stop() { expect_ok("stop"); }
void Drone::emergency() { expect_ok("emergency"); }

void Drone::move(Direction direction, int cm) {
    check_range(to_sdk(direction), cm, kMinMoveCm, kMaxMoveCm);
    expect_ok(std::string(to_sdk(direction)) + " " + std::to_string(cm));
}

void Drone::rotate_cw(int degrees) {
    check_range("cw", degrees, kMinRotateDeg, kMaxRotateDeg);
    expect_ok("cw " + std::to_string(degrees));
}

void Drone::rotate_ccw(int degrees) {
    check_range("ccw", degrees, kMinRotateDeg, kMaxRotateDeg);
    expect_ok("ccw " + std::to_string(degrees));
}

void Drone::flip(FlipDirection direction) {
    expect_ok(std::string("flip ") + to_sdk(direction));
}

void Drone::set_speed(int cm_per_s) {
    check_range("speed", cm_per_s, kMinSpeed, kMaxSpeed);
    expect_ok("speed " + std::to_string(cm_per_s));
}

void Drone::go(int x, int y, int z, int speed) {
    check_range("go x", x, -kMaxGoCm, kMaxGoCm);
    check_range("go y", y, -kMaxGoCm, kMaxGoCm);
    check_range("go z", z, -kMaxGoCm, kMaxGoCm);
    check_range("go speed", speed, kMinSpeed, kMaxSpeed);
    if (std::abs(x) < kMinGoSpan && std::abs(y) < kMinGoSpan && std::abs(z) < kMinGoSpan) {
        throw TelloError("go: x, y and z may not all be within -20..20");
    }
    expect_ok("go " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z) +
              " " + std::to_string(speed));
}

void Drone::rc(int left_right, int forward_back, int up_down, int yaw) {
    check_range("rc a", left_right, -kMaxRc, kMaxRc);
    check_range("rc b", forward_back, -kMaxRc, kMaxRc);
    check_range("rc c", up_down, -kMaxRc, kMaxRc);
    check_range("rc d", yaw, -kMaxRc, kMaxRc);
    transport_.send("rc " + std::to_string(left_right) + " " + std::to_string(forward_back) + " " +
                    std::to_string(up_down) + " " + std::to_string(yaw));
}

int Drone::battery() { return query_int("battery?"); }
int Drone::height_cm() { return query_int("height?") * 10; }
int Drone::wifi_snr() { return query_int("wifi?"); }
std::string Drone::temperature() { return query("temp?"); }
std::string Drone::flight_time() { return query("time?"); }
std::string Drone::sdk_version() { return query("sdk?"); }
std::string Drone::serial_number() { return query("sn?"); }
int Drone::speed() { return query_int("speed?"); }
std::string Drone::attitude() { return query("attitude?"); }
std::string Drone::barometer() { return query("baro?"); }
std::string Drone::tof_distance() { return query("tof?"); }

void Drone::expect_ok(const std::string& command) {
    const std::string reply = transport_.request(command, timeouts_.action);
    if (reply != "ok") {
        throw TelloError(command + ": " + (reply.empty() ? std::string("empty reply") : reply));
    }
}

std::string Drone::query(std::string_view command) {
    std::string reply = transport_.request(command, timeouts_.query);
    if (reply.starts_with("error") || reply.starts_with("unknown")) {
        throw TelloError(std::string(command) + ": " + reply);
    }
    return reply;
}

// Replies may carry units or a fraction after the number ("10dm", "100.0"): read the leading integer.
int Drone::query_int(std::string_view command) {
    const std::string reply = query(command);
    int value = 0;
    const char* end = reply.data() + reply.size();
    const auto [ptr, ec] = std::from_chars(reply.data(), end, value);
    if (ec != std::errc() || ptr == reply.data()) {
        throw TelloError(std::string(command) + ": not a number: '" + reply + "'");
    }
    return value;
}

}  // namespace tello
