#include "cli/flight_log.hpp"

#include "applog/log.hpp"

#include <cstdlib>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>

namespace cli
{

std::string describe(const tello::DroneState& s)
{
    std::ostringstream out;
    out << "bat=" << s.bat << "% h=" << s.h << "cm tof=" << s.tof << "cm temp=" << s.templ << ".."
        << s.temph << "C v=" << s.vgx << ',' << s.vgy << ',' << s.vgz << "dm/s pitch=" << s.pitch
        << " roll=" << s.roll << " yaw=" << s.yaw << " baro=" << std::fixed << std::setprecision(2)
        << s.baro << "m time=" << s.time << 's';
    return out.str();
}

TelemetryLog::TelemetryLog(const tello::StateListener& telemetry, Airborne airborne,
                           TelemetryLogSettings settings)
    : telemetry_(telemetry)
    , airborne_(std::move(airborne))
    , settings_(settings)
    , thread_(
          [this](const std::stop_token& stop)
          {
              run(stop);
          })
{
}

TelemetryLog::~TelemetryLog()
{
    thread_.request_stop();
}

void TelemetryLog::run(const std::stop_token& stop)
{
    applog::set_thread_name("telem");
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    std::optional<Clock::time_point> last_logged;
    bool lost = false;
    bool none_reported = false;
    while (!stop.stop_requested())
    {
        std::this_thread::sleep_for(settings_.check_period);
        const auto now = Clock::now();
        const auto snap = telemetry_.latest();
        if (!snap)
        {
            if (!none_reported && now - started > settings_.stale_after)
            {
                none_reported = true;
                applog::warn("telem", "no telemetry received yet");
            }
            continue;
        }
        const auto age = now - snap->received;
        if (age > settings_.stale_after)
        {
            if (!lost)
            {
                lost = true;
                applog::warn("telem", "telemetry lost: last packet " + applog::ms(age) +
                                          " ago, last state " + describe(snap->state));
            }
            continue;
        }
        if (lost)
        {
            lost = false;
            applog::info("telem", "telemetry back");
        }
        const bool flying = airborne_ && airborne_();
        const auto period = flying ? settings_.air_period : settings_.ground_period;
        if (!last_logged || now - *last_logged >= period)
        {
            last_logged = now;
            applog::info("telem", describe(snap->state));
        }
    }
}

std::filesystem::path default_log_dir()
{
    if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg != nullptr && *xdg != '\0')
    {
        return std::filesystem::path(xdg) / "tello" / "logs";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : ".") / ".local" / "state" / "tello" /
           "logs";
}

} // namespace cli
