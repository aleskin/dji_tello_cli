#include "drone/flight_guard.hpp"

#include "drone/errors.hpp"

#include "applog/log.hpp"

#include <utility>

namespace drone
{

FlightGuard::FlightGuard(DroneBase& drone, GuardSettings settings, BatterySource battery,
                         Notify notify, StallProbe stalled, TemperatureSource temperature)
    : drone_(drone)
    , settings_(settings)
    , battery_(std::move(battery))
    , notify_(std::move(notify))
    , stalled_(std::move(stalled))
    , temperature_(std::move(temperature))
    , thread_(
          [this](const std::stop_token& stop)
          {
              run(stop);
          })
{
}

FlightGuard::~FlightGuard()
{
    thread_.request_stop();
}

void FlightGuard::say(const std::string& message) const
{
    applog::warn("guard", message);
    if (notify_)
    {
        notify_(message);
    }
}

void FlightGuard::run(const std::stop_token& stop)
{
    applog::set_thread_name("guard");
    while (!stop.stop_requested())
    {
        std::this_thread::sleep_for(settings_.check_period);
        if (!check_stall())
        {
            check_keepalive();
        }
        check_battery();
        check_temperature();
    }
}

void FlightGuard::check_temperature()
{
    const auto temp = temperature_ ? temperature_() : std::nullopt;
    if (!temp)
    {
        return;
    }
    if (hot_ && *temp < settings_.warn_temp - settings_.temp_rearm)
    {
        applog::info("guard", "drone cooled down to " + std::to_string(*temp) + " C");
        hot_ = false;
        return;
    }
    if (hot_ || *temp < settings_.warn_temp)
    {
        return;
    }
    hot_ = true;
    say("drone overheating: " + std::to_string(*temp) +
        " C; land and let it cool, switch it off between flights (a hot Tello stops its motors)");
}

bool FlightGuard::check_stall()
{
    const auto stalled = stalled_ ? stalled_() : std::chrono::steady_clock::duration{};
    if (stalled <= std::chrono::steady_clock::duration::zero())
    {
        if (stuck_)
        {
            say("program responds again: keepalive back on");
            stuck_ = false;
        }
        return false;
    }
    if (stuck_)
    {
        return true; // landing sent once; no keepalive until the program moves again
    }
    if (stalled < settings_.stuck_after)
    {
        return false;
    }
    stuck_ = true;
    say("program stuck for " + applog::ms(stalled) + ": keepalive off, landing");
    try
    {
        drone_.land();
    }
    catch (const DroneError& e)
    {
        say(std::string("landing failed: ") + e.what());
    }
    return true;
}

void FlightGuard::check_keepalive()
{
    if (!keepalive_supported_ || drone_.idle_for() < settings_.keepalive_after)
    {
        return;
    }
    try
    {
        drone_.keepalive();
        if (lost_)
        {
            say("drone connected again: SDK mode restored by keepalive");
        }
        else if (keepalive_failing_)
        {
            say("keepalive: drone answers again");
        }
        keepalive_failing_ = false;
        lost_ = false;
    }
    catch (const NotSupported&)
    {
        applog::info("guard", "keepalive not supported by " + drone_.model());
        keepalive_supported_ = false;
    }
    catch (const DroneError& e)
    {
        if (!keepalive_failing_)
        { // once per outage, not every few seconds
            say(std::string("keepalive failed: ") + e.what());
        }
        keepalive_failing_ = true;
        check_lost();
    }
}

void FlightGuard::check_lost()
{
    if (lost_)
    {
        return;
    }
    const auto silent = drone_.silent_for();
    if (silent && *silent < settings_.lost_after)
    {
        return;
    }
    lost_ = true;
    const auto route = drone_.link_problem();
    const std::string why = route ? *route : "drone off, out of range or this PC left its Wi-Fi";
    if (!silent)
    {
        say("no drone: it has not answered yet (" + why +
            "); switch it on and join its Wi-Fi, the program connects by itself");
        return;
    }
    say("connection to the drone lost: no answer for " + applog::ms(*silent) + " (" + why +
        "); a flying Tello lands by itself 15 s after the last command it got");
}

void FlightGuard::check_battery()
{
    const auto level = battery_ ? battery_() : std::nullopt;
    if (!level)
    {
        return;
    }
    if (*level > settings_.warn_battery)
    {
        warned_ = false; // a fresh battery re-arms both alarms
        landed_ = false;
        land_failures_ = 0;
        next_land_ = {};
        return;
    }
    if (!warned_)
    {
        warned_ = true;
        say("battery low: " + std::to_string(*level) + " %, land soon");
    }
    const auto now = std::chrono::steady_clock::now();
    if (*level > settings_.land_battery || landed_ || now < next_land_)
    {
        return;
    }
    // Marked landed only once the drone confirms: a lost or refused 'land' is tried again.
    const std::string text = "battery " + std::to_string(*level) + " %: landing";
    if (land_failures_ == 0)
    {
        say(text);
    }
    else
    {
        applog::warn("guard", text + " (attempt " + std::to_string(land_failures_ + 1) + ")");
    }
    try
    {
        drone_.land();
        landed_ = true;
        if (land_failures_ > 0)
        {
            say("landing on low battery confirmed");
        }
    }
    catch (const DroneError& e)
    {
        if (land_failures_ == 0)
        {
            say(std::string("landing failed: ") + e.what() + "; trying again every " +
                applog::ms(settings_.land_retry));
        }
        else
        {
            applog::warn("guard", std::string("landing failed again: ") + e.what());
        }
        ++land_failures_;
        next_land_ = now + settings_.land_retry;
    }
}

} // namespace drone
