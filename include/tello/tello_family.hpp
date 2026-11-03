#pragma once

#include "drone/drone_base.hpp"
#include "tello/transport.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace tello
{

using drone::Capability;
using drone::Direction;
using drone::FlipDirection;

[[nodiscard]] std::string_view to_sdk(Direction d);
[[nodiscard]] char to_sdk(FlipDirection d);

// Tello SDK limits; checked before anything is sent.
inline constexpr int kMinMoveCm = 20;
inline constexpr int kMaxMoveCm = 500;
inline constexpr int kMinRotateDeg = 1;
inline constexpr int kMaxRotateDeg = 360;
inline constexpr int kMinSpeed = 10;
inline constexpr int kMaxSpeed = 100;
inline constexpr int kMaxGoCm = 500;
inline constexpr int kMinGoSpan = 20; // x, y, z may not all be inside -20..20
inline constexpr int kMaxRc = 100;

struct DroneTimeouts
{
    // The drone answers "ok" only after a move has finished.
    std::chrono::milliseconds action{20'000};
    std::chrono::milliseconds query{3'000};
    // Extra attempts for read queries only. Actions are never resent: a lost "ok" for
    // "forward 100" does not mean the drone did not fly, and a resend would fly twice.
    int query_retries = 2;
};

// Layer 2: the Tello text protocol over UDP, i.e. the SDK 1.3 command set every Tello has.
// Models (layer 3) add what their firmware has on top. Every method throws TelloError on an
// invalid argument (nothing is sent), a timeout or an unexpected reply.
class TelloFamily : public drone::DroneBase
{
public:
    [[nodiscard]] std::string family() const override
    {
        return "Tello";
    }
    [[nodiscard]] std::set<Capability> capabilities() const override;

    void connect() override; // "command": enter SDK mode, must be the first command
    void takeoff() override;
    void land() override;
    void emergency() override;

    void move(Direction direction, int cm) override;
    void rotate_cw(int degrees) override;
    void rotate_ccw(int degrees) override;
    void flip(FlipDirection direction) override;
    void set_speed(int cm_per_s) override;
    void go(int x, int y, int z, int speed) override;
    void rc(int left_right, int forward_back, int up_down, int yaw) override;

    [[nodiscard]] int battery() override;
    [[nodiscard]] int height_cm() override; // drone reports decimetres
    [[nodiscard]] int wifi_snr() override;
    [[nodiscard]] std::string temperature() override; // "63~65C"
    [[nodiscard]] std::string flight_time() override; // "12s"
    [[nodiscard]] int speed() override;
    [[nodiscard]] std::string attitude() override;     // "pitch:0;roll:0;yaw:0;"
    [[nodiscard]] std::string barometer() override;    // metres
    [[nodiscard]] std::string tof_distance() override; // "100mm"

    void keepalive() override; // "command": harmless in SDK mode, resets the 15 s timer

    void start_video() override; // "streamon"
    void stop_video() override;  // "streamoff"
    [[nodiscard]] std::string video_source() const override;
    // The drone always streams to port 11111 of this PC; the simulator can use another one.
    void set_video_port(std::uint16_t port);

protected:
    TelloFamily(std::shared_ptr<ITransport> transport, DroneTimeouts timeouts);

    void expect_ok(const std::string& command);
    std::string query(std::string_view command);
    int query_int(std::string_view command);

private:
    std::shared_ptr<ITransport> transport_;
    DroneTimeouts timeouts_;
    std::uint16_t video_port_;
};

// Layer 3: the original Tello on its last firmware, SDK 1.3 (no stop, sdk?, sn?, mission pads).
class TelloClassic final : public TelloFamily
{
public:
    explicit TelloClassic(std::shared_ptr<ITransport> transport, DroneTimeouts timeouts = {})
        : TelloFamily(std::move(transport), timeouts)
    {
    }

    [[nodiscard]] std::string model() const override
    {
        return "Tello (SDK 1.3)";
    }
};

// Layer 3: Tello EDU, SDK 2.0 on top of the family set. Mission pads come with TEL-00023.
class TelloEdu final : public TelloFamily
{
public:
    explicit TelloEdu(std::shared_ptr<ITransport> transport, DroneTimeouts timeouts = {})
        : TelloFamily(std::move(transport), timeouts)
    {
    }

    [[nodiscard]] std::string model() const override
    {
        return "Tello EDU (SDK 2.0)";
    }
    [[nodiscard]] std::set<Capability> capabilities() const override;

    void hover() override; // "stop"
    [[nodiscard]] std::string sdk_version() override;
    [[nodiscard]] std::string serial_number() override;
};

} // namespace tello
