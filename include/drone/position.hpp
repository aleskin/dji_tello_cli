#pragma once

#include "drone/drone_base.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace drone
{

// Where the drone is relative to its takeoff point: x forward along the takeoff heading,
// y to the right, z up. Heading is clockwise from the takeoff heading.
struct Position
{
    double x_cm = 0.0;
    double y_cm = 0.0;
    double z_cm = 0.0;
    int heading_deg = 0;   // 0..359
    double error_cm = 0.0; // estimated horizontal error
};

// Measured motion in the drone's body frame (forward, right, up), e.g. from telemetry.
struct MotionSample
{
    double forward_cm_s = 0.0;
    double right_cm_s = 0.0;
    double up_cm_s = 0.0;
    std::optional<int> yaw_deg;      // absolute, as the drone's sensors report it
    std::optional<double> height_cm; // measured height wins over integrating up_cm_s
    std::chrono::duration<double> dt{0.0};
};

// Base of the position hierarchy, vendor-neutral like DroneBase. A source is told about every
// completed movement command; one that does not need a notification ignores it.
// Dead reckoning lives here (DeadReckoning); satellite (TEL-00034) and camera (TEL-00035)
// sources come later.
class PositionSource
{
public:
    PositionSource(const PositionSource&) = delete;
    PositionSource& operator=(const PositionSource&) = delete;
    PositionSource(PositionSource&&) = delete;
    PositionSource& operator=(PositionSource&&) = delete;
    virtual ~PositionSource() = default;

    [[nodiscard]] virtual std::string name() const = 0; // "dead reckoning"
    [[nodiscard]] virtual Position position() const = 0;

    virtual void on_takeoff() // the takeoff point becomes the origin
    {
    }
    virtual void on_land()
    {
    }
    virtual void on_move(Direction, int)
    {
    }
    virtual void on_rotate(int) // degrees, clockwise positive
    {
    }
    virtual void on_go(int, int, int) // body frame: forward, left, up (cm)
    {
    }
    virtual void on_flip()
    {
    }
    // Manual flying (rc) starts: commands no longer say where the drone goes, samples do.
    virtual void on_free_flight()
    {
    }
    virtual void on_motion(const MotionSample&)
    {
    }

protected:
    PositionSource() = default;
};

} // namespace drone
