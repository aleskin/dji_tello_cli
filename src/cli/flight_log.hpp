#pragma once

#include "tello/state.hpp"
#include "tello/state_listener.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

namespace cli
{

struct TelemetryLogSettings
{
    std::chrono::milliseconds air_period{1'000};     // a snapshot this often in the air
    std::chrono::milliseconds ground_period{10'000}; // and this often on the ground
    std::chrono::milliseconds stale_after{2'000};    // no packet this long: telemetry lost
    std::chrono::milliseconds check_period{100};
};

// "bat=87% h=120cm tof=130cm temp=60..63C v=0,0,0dm/s pitch=0 roll=0 yaw=90 baro=12.34m time=15s"
[[nodiscard]] std::string describe(const tello::DroneState& state);

// Background writer of telemetry snapshots to the session log, plus "telemetry lost / back".
class TelemetryLog
{
public:
    using Airborne = std::function<bool()>; // called from the log thread

    TelemetryLog(const tello::StateListener& telemetry, Airborne airborne,
                 TelemetryLogSettings settings = {});
    ~TelemetryLog();

    TelemetryLog(const TelemetryLog&) = delete;
    TelemetryLog& operator=(const TelemetryLog&) = delete;
    TelemetryLog(TelemetryLog&&) = delete;
    TelemetryLog& operator=(TelemetryLog&&) = delete;

private:
    void run(const std::stop_token& stop);

    const tello::StateListener& telemetry_;
    Airborne airborne_;
    TelemetryLogSettings settings_;
    std::jthread thread_; // last: started after everything it uses exists
};

// $XDG_STATE_HOME/tello/logs, or ~/.local/state/tello/logs.
[[nodiscard]] std::filesystem::path default_log_dir();

} // namespace cli
