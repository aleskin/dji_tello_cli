#pragma once

#include "drone/drone_base.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace drone
{

struct GuardSettings
{
    // Tello auto-lands after 15 s without commands: talk at least every keepalive_after.
    std::chrono::milliseconds keepalive_after{5'000};
    std::chrono::milliseconds check_period{200};
    int warn_battery = 15; // percent: warning once
    int land_battery = 10; // percent: land once
};

// Background safety for a connected drone, working through DroneBase only:
// keepalive when the user is quiet, a warning and then a landing on low battery.
class FlightGuard
{
public:
    using BatterySource = std::function<std::optional<int>()>; // nullopt = unknown right now
    using Notify = std::function<void(const std::string&)>;    // called from the guard thread

    FlightGuard(DroneBase& drone, GuardSettings settings, BatterySource battery, Notify notify);
    ~FlightGuard();

    FlightGuard(const FlightGuard&) = delete;
    FlightGuard& operator=(const FlightGuard&) = delete;

private:
    void run(const std::stop_token& stop);
    void check_keepalive();
    void check_battery();
    void say(const std::string& message) const;

    DroneBase& drone_;
    GuardSettings settings_;
    BatterySource battery_;
    Notify notify_;
    bool keepalive_supported_ = true;
    bool keepalive_failing_ = false;
    bool warned_ = false;
    bool landed_ = false;
    std::jthread thread_; // last: started after everything it uses exists
};

} // namespace drone
