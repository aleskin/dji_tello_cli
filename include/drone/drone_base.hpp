#pragma once

#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <string_view>

namespace drone
{

enum class Direction
{
    Up,
    Down,
    Left,
    Right,
    Forward,
    Back
};
enum class FlipDirection
{
    Left,
    Right,
    Forward,
    Back
};

// What a model can do. The control layer asks before offering a command to the user.
enum class Capability
{
    Takeoff,
    Land,
    Emergency,
    Hover,
    Move,
    Rotate,
    Flip,
    SetSpeed,
    GoTo,
    Rc,
    Battery,
    Height,
    Temperature,
    FlightTime,
    Wifi,
    SdkVersion,
    SerialNumber,
    Speed,
    Attitude,
    Barometer,
    Tof,
};

[[nodiscard]] std::string_view to_string(Capability c);

// Layer 1 of three: the vendor-neutral drone the control layer (CLI, missions, keepalive) talks to.
// Layer 2 is a family (one vendor protocol, e.g. TelloFamily), layer 3 a concrete model.
// Every command defaults to throwing NotSupported, so a model overrides only what it has.
class DroneBase
{
public:
    virtual ~DroneBase() = default;
    DroneBase(const DroneBase&) = delete;
    DroneBase& operator=(const DroneBase&) = delete;

    [[nodiscard]] virtual std::string family() const = 0; // "Tello"
    [[nodiscard]] virtual std::string model() const = 0;  // "Tello (SDK 1.3)"
    [[nodiscard]] virtual std::set<Capability> capabilities() const
    {
        return {};
    }
    [[nodiscard]] bool supports(Capability c) const
    {
        return capabilities().contains(c);
    }

    virtual void connect();
    virtual void takeoff();
    virtual void land();
    virtual void emergency(); // motors off immediately
    virtual void hover();     // stop and hold position

    virtual void move(Direction direction, int cm);
    virtual void rotate_cw(int degrees);
    virtual void rotate_ccw(int degrees);
    virtual void flip(FlipDirection direction);
    virtual void set_speed(int cm_per_s);
    virtual void go(int x, int y, int z, int speed);
    virtual void rc(int left_right, int forward_back, int up_down, int yaw); // no reply

    [[nodiscard]] virtual int battery(); // percent
    [[nodiscard]] virtual int height_cm();
    [[nodiscard]] virtual std::string temperature();
    [[nodiscard]] virtual std::string flight_time();
    [[nodiscard]] virtual int wifi_snr();
    [[nodiscard]] virtual std::string sdk_version();
    [[nodiscard]] virtual std::string serial_number();
    [[nodiscard]] virtual int speed(); // cm/s
    [[nodiscard]] virtual std::string attitude();
    [[nodiscard]] virtual std::string barometer();
    [[nodiscard]] virtual std::string tof_distance();

    // Cheapest command that resets the drone's auto-land timer (Tello: 15 s of silence).
    virtual void keepalive();

    // Time since the model last talked to the drone (zero before the first command);
    // FlightGuard sends keepalive() after a pause.
    [[nodiscard]] std::chrono::steady_clock::duration idle_for() const;

protected:
    DroneBase() = default;

    // Families call this for every command sent, reply or not.
    void mark_activity();

    // Throws NotSupported naming this model and the command.
    [[noreturn]] void unsupported(std::string_view command) const;

private:
    // steady_clock ticks; atomic because the keepalive thread reads it while the CLI commands.
    std::atomic<std::chrono::steady_clock::rep> last_activity_{0};
};

} // namespace drone
