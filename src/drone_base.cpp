#include "drone/drone_base.hpp"

#include "drone/errors.hpp"
#include "drone/position.hpp"

#include <string>

namespace drone
{

std::string_view to_string(Capability c)
{
    switch (c)
    {
    case Capability::Takeoff:
        return "takeoff";
    case Capability::Land:
        return "land";
    case Capability::Emergency:
        return "emergency";
    case Capability::Hover:
        return "hover";
    case Capability::Move:
        return "move";
    case Capability::Rotate:
        return "rotate";
    case Capability::Flip:
        return "flip";
    case Capability::SetSpeed:
        return "set speed";
    case Capability::GoTo:
        return "go";
    case Capability::Rc:
        return "rc";
    case Capability::Battery:
        return "battery";
    case Capability::Height:
        return "height";
    case Capability::Temperature:
        return "temperature";
    case Capability::FlightTime:
        return "flight time";
    case Capability::Wifi:
        return "wifi";
    case Capability::SdkVersion:
        return "sdk version";
    case Capability::SerialNumber:
        return "serial number";
    case Capability::Speed:
        return "speed";
    case Capability::Attitude:
        return "attitude";
    case Capability::Barometer:
        return "barometer";
    case Capability::Tof:
        return "tof";
    case Capability::Video:
        return "video";
    case Capability::Position:
        return "position";
    }
    return "?";
}

void DroneBase::unsupported(std::string_view command) const
{
    throw NotSupported(model() + ": '" + std::string(command) + "' is not supported");
}

void DroneBase::connect()
{
    unsupported("connect");
}
void DroneBase::takeoff()
{
    unsupported("takeoff");
}
void DroneBase::land()
{
    unsupported("land");
}
void DroneBase::emergency()
{
    unsupported("emergency");
}
void DroneBase::hover()
{
    unsupported("hover");
}
void DroneBase::move(Direction, int)
{
    unsupported("move");
}
void DroneBase::rotate_cw(int)
{
    unsupported("cw");
}
void DroneBase::rotate_ccw(int)
{
    unsupported("ccw");
}
void DroneBase::flip(FlipDirection)
{
    unsupported("flip");
}
void DroneBase::set_speed(int)
{
    unsupported("speed");
}
void DroneBase::go(int, int, int, int)
{
    unsupported("go");
}
void DroneBase::rc(int, int, int, int)
{
    unsupported("rc");
}
int DroneBase::battery()
{
    unsupported("battery");
}
int DroneBase::height_cm()
{
    unsupported("height");
}
std::string DroneBase::temperature()
{
    unsupported("temperature");
}
std::string DroneBase::flight_time()
{
    unsupported("flight time");
}
int DroneBase::wifi_snr()
{
    unsupported("wifi");
}
std::string DroneBase::sdk_version()
{
    unsupported("sdk version");
}
std::string DroneBase::serial_number()
{
    unsupported("serial number");
}
int DroneBase::speed()
{
    unsupported("speed");
}
std::string DroneBase::attitude()
{
    unsupported("attitude");
}
std::string DroneBase::barometer()
{
    unsupported("barometer");
}
std::string DroneBase::tof_distance()
{
    unsupported("tof");
}
void DroneBase::keepalive()
{
    unsupported("keepalive");
}
void DroneBase::start_video()
{
    unsupported("video");
}
void DroneBase::stop_video()
{
    unsupported("video");
}
std::string DroneBase::video_source() const
{
    unsupported("video");
}

std::chrono::steady_clock::duration DroneBase::idle_for() const
{
    if (last_activity_.load() == 0)
    {
        return {}; // never talked to the drone: nothing to keep alive yet
    }
    const auto last = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(last_activity_.load()));
    return std::chrono::steady_clock::now() - last;
}

void DroneBase::mark_activity()
{
    last_activity_.store(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::unique_ptr<PositionSource> DroneBase::make_position_source() const
{
    return nullptr;
}

} // namespace drone
