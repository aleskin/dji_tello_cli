#include "tello/tello_family.hpp"

#include "drone/dead_reckoning.hpp"

#include "tello/errors.hpp"
#include "tello/udp_transport.hpp"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <ctime>
#include <utility>

namespace tello
{
namespace
{

void check_range(std::string_view what, int value, int min, int max)
{
    if (value < min || value > max)
    {
        throw TelloError(std::string(what) + " must be " + std::to_string(min) + ".." +
                         std::to_string(max) + ", got " + std::to_string(value));
    }
}

} // namespace

std::string_view to_sdk(Direction d)
{
    switch (d)
    {
    case Direction::Up:
        return "up";
    case Direction::Down:
        return "down";
    case Direction::Left:
        return "left";
    case Direction::Right:
        return "right";
    case Direction::Forward:
        return "forward";
    case Direction::Back:
        return "back";
    }
    return "?";
}

char to_sdk(FlipDirection d)
{
    switch (d)
    {
    case FlipDirection::Left:
        return 'l';
    case FlipDirection::Right:
        return 'r';
    case FlipDirection::Forward:
        return 'f';
    case FlipDirection::Back:
        return 'b';
    }
    return '?';
}

TelloFamily::TelloFamily(std::shared_ptr<ITransport> transport, DroneTimeouts timeouts)
    : transport_(std::move(transport))
    , timeouts_(timeouts)
    , video_port_(kVideoPort)
{
    if (!transport_)
    {
        throw TelloError("Tello: no transport");
    }
}

std::set<Capability> TelloFamily::capabilities() const
{
    return {
        Capability::Takeoff,    Capability::Land,    Capability::Emergency, Capability::Move,
        Capability::Rotate,     Capability::Flip,    Capability::SetSpeed,  Capability::GoTo,
        Capability::Rc,         Capability::Battery, Capability::Height,    Capability::Temperature,
        Capability::FlightTime, Capability::Wifi,    Capability::Speed,     Capability::Attitude,
        Capability::Barometer,  Capability::Tof,     Capability::Video,     Capability::Position};
}

std::unique_ptr<drone::PositionSource> TelloFamily::make_position_source() const
{
    return std::make_unique<drone::DeadReckoning>(kTakeoffHeightCm);
}

void TelloFamily::connect()
{
    expect_ok("command");
}
void TelloFamily::takeoff()
{
    expect_ok("takeoff");
}
void TelloFamily::land()
{
    expect_ok("land");
}
void TelloFamily::emergency()
{
    expect_ok("emergency");
}

void TelloFamily::move(Direction direction, int cm)
{
    check_range(to_sdk(direction), cm, kMinMoveCm, kMaxMoveCm);
    expect_ok(std::string(to_sdk(direction)) + " " + std::to_string(cm));
}

void TelloFamily::rotate_cw(int degrees)
{
    check_range("cw", degrees, kMinRotateDeg, kMaxRotateDeg);
    expect_ok("cw " + std::to_string(degrees));
}

void TelloFamily::rotate_ccw(int degrees)
{
    check_range("ccw", degrees, kMinRotateDeg, kMaxRotateDeg);
    expect_ok("ccw " + std::to_string(degrees));
}

void TelloFamily::flip(FlipDirection direction)
{
    expect_ok(std::string("flip ") + to_sdk(direction));
}

void TelloFamily::set_speed(int cm_per_s)
{
    check_range("speed", cm_per_s, kMinSpeed, kMaxSpeed);
    expect_ok("speed " + std::to_string(cm_per_s));
}

void TelloFamily::go(int x, int y, int z, int speed)
{
    check_range("go x", x, -kMaxGoCm, kMaxGoCm);
    check_range("go y", y, -kMaxGoCm, kMaxGoCm);
    check_range("go z", z, -kMaxGoCm, kMaxGoCm);
    check_range("go speed", speed, kMinSpeed, kMaxSpeed);
    if (std::abs(x) < kMinGoSpan && std::abs(y) < kMinGoSpan && std::abs(z) < kMinGoSpan)
    {
        throw TelloError("go: x, y and z may not all be within -20..20");
    }
    expect_ok("go " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z) + " " +
              std::to_string(speed));
}

void TelloFamily::rc(int left_right, int forward_back, int up_down, int yaw)
{
    check_range("rc a", left_right, -kMaxRc, kMaxRc);
    check_range("rc b", forward_back, -kMaxRc, kMaxRc);
    check_range("rc c", up_down, -kMaxRc, kMaxRc);
    check_range("rc d", yaw, -kMaxRc, kMaxRc);
    mark_activity();
    transport_->send("rc " + std::to_string(left_right) + " " + std::to_string(forward_back) + " " +
                     std::to_string(up_down) + " " + std::to_string(yaw));
}

int TelloFamily::battery()
{
    return query_int("battery?");
}
int TelloFamily::height_cm()
{
    return query_int("height?") * 10;
}
int TelloFamily::wifi_snr()
{
    return query_int("wifi?");
}
std::string TelloFamily::temperature()
{
    return query("temp?");
}
std::string TelloFamily::flight_time()
{
    return query("time?");
}
int TelloFamily::speed()
{
    return query_int("speed?");
}
std::string TelloFamily::attitude()
{
    return query("attitude?");
}
std::string TelloFamily::barometer()
{
    return query("baro?");
}
std::string TelloFamily::tof_distance()
{
    return query("tof?");
}

void TelloFamily::land_now() noexcept
{
    transport_->send_now("rc 0 0 0 0");
    for (int i = 0; i < 3; ++i)
    {
        transport_->send_now("land");
        const timespec pause{0, 100'000'000};
        ::nanosleep(&pause, nullptr);
    }
}

void TelloFamily::keepalive()
{
    expect_ok("command", false);
}

void TelloFamily::start_video()
{
    expect_ok("streamon");
}

void TelloFamily::stop_video()
{
    expect_ok("streamoff");
}

std::string TelloFamily::video_source() const
{
    return "udp://0.0.0.0:" + std::to_string(video_port_);
}

void TelloFamily::set_video_port(std::uint16_t port)
{
    video_port_ = port;
}

void TelloFamily::expect_ok(const std::string& command, bool counts_as_command)
{
    mark_activity(counts_as_command);
    const std::string reply = transport_->request(command, timeouts_.action);
    if (reply != "ok")
    {
        throw TelloError(command + ": " + (reply.empty() ? std::string("empty reply") : reply));
    }
}

std::string TelloFamily::query(std::string_view command)
{
    std::string reply;
    for (int attempt = 0;; ++attempt)
    {
        mark_activity();
        try
        {
            reply = transport_->request(command, timeouts_.query);
            break;
        }
        catch (const drone::NoReply&)
        {
            if (attempt >= timeouts_.query_retries)
            {
                throw;
            }
        }
    }
    if (reply.starts_with("error") || reply.starts_with("unknown"))
    {
        throw TelloError(std::string(command) + ": " + reply);
    }
    return reply;
}

// Replies may carry units or a fraction after the number ("10dm", "100.0"): read the leading
// integer.
int TelloFamily::query_int(std::string_view command)
{
    const std::string reply = query(command);
    int value = 0;
    const char* end = reply.data() + reply.size();
    const auto [ptr, ec] = std::from_chars(reply.data(), end, value);
    if (ec != std::errc() || ptr == reply.data())
    {
        throw TelloError(std::string(command) + ": not a number: '" + reply + "'");
    }
    return value;
}

std::set<Capability> TelloEdu::capabilities() const
{
    auto caps = TelloFamily::capabilities();
    caps.insert({Capability::Hover, Capability::SdkVersion, Capability::SerialNumber});
    return caps;
}

void TelloEdu::hover()
{
    expect_ok("stop");
}
std::string TelloEdu::sdk_version()
{
    return query("sdk?");
}
std::string TelloEdu::serial_number()
{
    return query("sn?");
}

} // namespace tello
