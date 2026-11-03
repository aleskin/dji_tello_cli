#include "drone/dead_reckoning.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace drone
{
namespace
{

double normalize(double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

} // namespace

DeadReckoning::DeadReckoning(int takeoff_height_cm)
    : takeoff_height_cm_(takeoff_height_cm)
{
}

Position DeadReckoning::position() const
{
    return {x_, y_, z_, static_cast<int>(std::lround(heading_)) % 360, error_};
}

void DeadReckoning::on_takeoff()
{
    x_ = 0.0;
    y_ = 0.0;
    z_ = takeoff_height_cm_;
    heading_ = 0.0;
    error_ = 0.0;
    yaw_offset_.reset();
}

void DeadReckoning::on_land()
{
    z_ = 0.0;
}

void DeadReckoning::on_move(Direction direction, int cm)
{
    switch (direction)
    {
    case Direction::Up:
        z_ += cm;
        break;
    case Direction::Down:
        z_ = std::max(0.0, z_ - cm);
        break;
    case Direction::Forward:
        shift(cm, 0.0, kCommandError);
        break;
    case Direction::Back:
        shift(-cm, 0.0, kCommandError);
        break;
    case Direction::Right:
        shift(0.0, cm, kCommandError);
        break;
    case Direction::Left:
        shift(0.0, -cm, kCommandError);
        break;
    }
}

void DeadReckoning::on_rotate(int cw_degrees)
{
    heading_ = normalize(heading_ + cw_degrees);
}

void DeadReckoning::on_go(int forward, int left, int up)
{
    shift(forward, -left, kCommandError);
    z_ = std::max(0.0, z_ + up);
}

void DeadReckoning::on_flip()
{
    error_ += kFlipError;
}

void DeadReckoning::on_free_flight()
{
    yaw_offset_.reset();
}

void DeadReckoning::on_motion(const MotionSample& s)
{
    if (s.yaw_deg)
    {
        if (!yaw_offset_)
        {
            yaw_offset_ = *s.yaw_deg - heading_;
        }
        heading_ = normalize(*s.yaw_deg - *yaw_offset_);
    }
    const double dt = s.dt.count();
    shift(s.forward_cm_s * dt, s.right_cm_s * dt, kIntegrationError);
    z_ = s.height_cm ? *s.height_cm : std::max(0.0, z_ + s.up_cm_s * dt);
}

void DeadReckoning::shift(double forward, double right, double error_share)
{
    const double rad = heading_ * std::numbers::pi / 180.0;
    x_ += forward * std::cos(rad) - right * std::sin(rad);
    y_ += forward * std::sin(rad) + right * std::cos(rad);
    error_ += error_share * std::hypot(forward, right);
}

} // namespace drone
