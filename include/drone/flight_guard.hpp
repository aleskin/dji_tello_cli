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
    // Hang watchdog: a command stalled this long means the program is stuck. Above the longest
    // legal wait for a reply (action timeout 20 s, queries 3 x 3 s).
    std::chrono::milliseconds stuck_after{45'000};
    // A low-battery landing that failed is tried again this often while the battery stays low.
    std::chrono::milliseconds land_retry{3'000};
    // No answer from the drone this long while keepalive goes out: the link is lost (drone off,
    // out of range, this PC left its Wi-Fi). Tello lands by itself after 15 s without commands.
    std::chrono::milliseconds lost_after{15'000};
    // Highest drone temperature (C) for an overheating warning; again after cooling by temp_rearm.
    int warn_temp = 85;
    int temp_rearm = 5;
};

// Background safety for a connected drone, working through DroneBase only:
// keepalive when the user is quiet, a warning and then a landing on low battery, a landing
// when the program hangs (keepalive would otherwise hold a stuck program's drone in the air),
// and a warning when the drone overheats.
class FlightGuard
{
public:
    using BatterySource = std::function<std::optional<int>()>; // nullopt = unknown right now
    using Notify = std::function<void(const std::string&)>;    // called from the guard thread
    // How long the program's current command has been stuck; zero: not stuck (or unknown).
    using StallProbe = std::function<std::chrono::steady_clock::duration()>;
    using TemperatureSource = std::function<std::optional<int>()>; // C; nullopt = unknown now

    FlightGuard(DroneBase& drone, GuardSettings settings, BatterySource battery, Notify notify,
                StallProbe stalled = {}, TemperatureSource temperature = {});
    ~FlightGuard();

    FlightGuard(const FlightGuard&) = delete;
    FlightGuard& operator=(const FlightGuard&) = delete;
    FlightGuard(FlightGuard&&) = delete;
    FlightGuard& operator=(FlightGuard&&) = delete;

private:
    void run(const std::stop_token& stop);
    bool check_stall(); // true while the program is stuck: no keepalive then
    void check_keepalive();
    void check_lost(); // after a failed keepalive: says once that the drone is gone
    void check_battery();
    void check_temperature();
    void say(const std::string& message) const;

    DroneBase& drone_;
    GuardSettings settings_;
    BatterySource battery_;
    Notify notify_;
    StallProbe stalled_;
    TemperatureSource temperature_;
    bool stuck_ = false;
    bool hot_ = false; // overheating said; again once it cooled down
    bool keepalive_supported_ = true;
    bool keepalive_failing_ = false;
    bool lost_ = false; // "connection lost" said; "connected again" once keepalive gets through
    bool warned_ = false;
    bool landed_ = false;   // low-battery landing confirmed
    int land_failures_ = 0; // failed low-battery landings in a row
    std::chrono::steady_clock::time_point next_land_{};
    std::jthread thread_; // last: started after everything it uses exists
};

} // namespace drone
