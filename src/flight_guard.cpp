#include "drone/flight_guard.hpp"

#include "drone/errors.hpp"

#include <utility>

namespace drone {

FlightGuard::FlightGuard(DroneBase& drone, GuardSettings settings, BatterySource battery,
                         Notify notify)
    : drone_(drone),
      settings_(settings),
      battery_(std::move(battery)),
      notify_(std::move(notify)),
      thread_([this](const std::stop_token& stop) { run(stop); }) {}

FlightGuard::~FlightGuard() { thread_.request_stop(); }

void FlightGuard::say(const std::string& message) const {
    if (notify_) {
        notify_(message);
    }
}

void FlightGuard::run(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        std::this_thread::sleep_for(settings_.check_period);
        check_keepalive();
        check_battery();
    }
}

void FlightGuard::check_keepalive() {
    if (!keepalive_supported_ || drone_.idle_for() < settings_.keepalive_after) {
        return;
    }
    try {
        drone_.keepalive();
        if (keepalive_failing_) {
            say("keepalive: drone answers again");
        }
        keepalive_failing_ = false;
    } catch (const NotSupported&) {
        keepalive_supported_ = false;
    } catch (const DroneError& e) {
        if (!keepalive_failing_) {  // once per outage, not every few seconds
            say(std::string("keepalive failed: ") + e.what());
        }
        keepalive_failing_ = true;
    }
}

void FlightGuard::check_battery() {
    const auto level = battery_ ? battery_() : std::nullopt;
    if (!level) {
        return;
    }
    if (*level > settings_.warn_battery) {
        warned_ = false;  // a fresh battery re-arms both alarms
        landed_ = false;
        return;
    }
    if (!warned_) {
        warned_ = true;
        say("battery low: " + std::to_string(*level) + " %, land soon");
    }
    if (*level <= settings_.land_battery && !landed_) {
        landed_ = true;
        say("battery " + std::to_string(*level) + " %: landing");
        try {
            drone_.land();
        } catch (const DroneError& e) {
            say(std::string("landing failed: ") + e.what());
        }
    }
}

}  // namespace drone
