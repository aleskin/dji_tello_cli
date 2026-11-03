#pragma once

#include "drone/position.hpp"

#include <optional>
#include <string>

namespace drone
{

// Position "in memory" for drones without satellites (Tello): completed commands move it exactly,
// manual flight integrates measured speed. The error estimate grows with the distance flown.
class DeadReckoning final : public PositionSource
{
public:
    explicit DeadReckoning(int takeoff_height_cm);

    [[nodiscard]] std::string name() const override
    {
        return "dead reckoning";
    }
    [[nodiscard]] Position position() const override;

    void on_takeoff() override;
    void on_land() override;
    void on_move(Direction direction, int cm) override;
    void on_rotate(int cw_degrees) override;
    void on_go(int forward, int left, int up) override;
    void on_flip() override;
    void on_free_flight() override;
    void on_motion(const MotionSample& sample) override;

    static constexpr double kCommandError = 0.05;     // of the distance a command flies
    static constexpr double kIntegrationError = 0.15; // of the distance integrated from speed
    static constexpr double kFlipError = 30.0;        // cm per flip

private:
    void shift(double forward, double right, double error_share);

    int takeoff_height_cm_;
    double x_ = 0.0;
    double y_ = 0.0;
    double z_ = 0.0;
    double heading_ = 0.0; // 0..360
    double error_ = 0.0;
    std::optional<double> yaw_offset_; // sensor yaw minus our heading, set by the first sample
};

} // namespace drone
