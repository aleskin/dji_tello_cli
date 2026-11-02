#pragma once

#include "tello/transport.hpp"

#include <chrono>
#include <string>
#include <string_view>

namespace tello {

enum class Direction { Up, Down, Left, Right, Forward, Back };
enum class FlipDirection { Left, Right, Forward, Back };

[[nodiscard]] std::string_view to_sdk(Direction d);
[[nodiscard]] char to_sdk(FlipDirection d);

// Tello SDK 2.0 limits; checked before anything is sent.
inline constexpr int kMinMoveCm = 20;
inline constexpr int kMaxMoveCm = 500;
inline constexpr int kMinRotateDeg = 1;
inline constexpr int kMaxRotateDeg = 360;
inline constexpr int kMinSpeed = 10;
inline constexpr int kMaxSpeed = 100;
inline constexpr int kMaxGoCm = 500;
inline constexpr int kMinGoSpan = 20;  // x, y, z may not all be inside -20..20
inline constexpr int kMaxRc = 100;

struct DroneTimeouts {
    // The drone answers "ok" only after a move has finished.
    std::chrono::milliseconds action{20'000};
    std::chrono::milliseconds query{3'000};
};

// Tello SDK command layer. Every method throws TelloError on an invalid argument
// (nothing is sent), a timeout or a reply other than the expected one.
class Drone {
public:
    explicit Drone(ITransport& transport, DroneTimeouts timeouts = {});

    void connect();  // "command": enter SDK mode, must be the first command
    void takeoff();
    void land();
    void stop();       // hover in place
    void emergency();  // motors off immediately

    void move(Direction direction, int cm);
    void rotate_cw(int degrees);
    void rotate_ccw(int degrees);
    void flip(FlipDirection direction);
    void set_speed(int cm_per_s);
    void go(int x, int y, int z, int speed);
    void rc(int left_right, int forward_back, int up_down, int yaw);  // no reply

    [[nodiscard]] int battery();      // percent
    [[nodiscard]] int height_cm();    // drone reports decimetres
    [[nodiscard]] int wifi_snr();
    [[nodiscard]] std::string temperature();  // "63~65C"
    [[nodiscard]] std::string flight_time();  // "12s"
    [[nodiscard]] std::string sdk_version();
    [[nodiscard]] std::string serial_number();
    [[nodiscard]] int speed();                // cm/s, as set by "speed x"
    [[nodiscard]] std::string attitude();     // "pitch:0;roll:0;yaw:0;"
    [[nodiscard]] std::string barometer();    // metres, "0.12"
    [[nodiscard]] std::string tof_distance(); // time-of-flight sensor, "100mm"

private:
    void expect_ok(const std::string& command);
    std::string query(std::string_view command);
    int query_int(std::string_view command);

    ITransport& transport_;
    DroneTimeouts timeouts_;
};

}  // namespace tello
