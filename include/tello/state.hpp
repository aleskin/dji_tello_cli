#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tello {

// Telemetry the drone sends to UDP 8890 about 10 times per second.
struct DroneState {
    int pitch = 0;  // degrees
    int roll = 0;
    int yaw = 0;
    int vgx = 0;  // speed, dm/s
    int vgy = 0;
    int vgz = 0;
    int templ = 0;  // lowest / highest temperature, C
    int temph = 0;
    int tof = 0;   // time-of-flight distance to the ground, cm
    int h = 0;     // height above the takeoff point, cm
    int bat = 0;   // battery, percent
    double baro = 0.0;  // barometer altitude, m
    int time = 0;  // motors-on time, s
    double agx = 0.0;  // acceleration, 0.001 g
    double agy = 0.0;
    double agz = 0.0;

    bool operator==(const DroneState&) const = default;
};

// "pitch:0;roll:0;...;\r\n" → DroneState. Unknown keys (mid, x, mpry ...) are ignored.
// std::nullopt when a known key has a non-numeric value or no known key is present.
[[nodiscard]] std::optional<DroneState> parse_state(std::string_view text);

// Inverse of parse_state, in the drone's field order (used by the simulator).
// SDK 2.0 drones (Tello EDU) prefix the packet with mission-pad fields: mid, x, y, z, mpry.
[[nodiscard]] std::string format_state(const DroneState& state, bool mission_pad_fields = true);

}  // namespace tello
