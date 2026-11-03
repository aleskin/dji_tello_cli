#pragma once

#include "tello/state.hpp"
#include "tello/udp_socket.hpp"

#include <chrono>
#include <atomic>
#include <cstdint>
#include <functional>
#include <random>
#include <set>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace tello::sim {

using Clock = std::chrono::steady_clock;

// Called from the command thread for every datagram: command text and reply (nullopt = silent).
using CommandLog = std::function<void(std::string_view, const std::optional<std::string>&)>;

// Which drone to imitate. Tello: SDK 1.3 firmware (the owner's drone, no updates),
// TelloEdu: SDK 2.0 (stop, sdk?, sn?, mission-pad telemetry fields).
enum class SimModelKind { Tello, TelloEdu };

// "tello" / "tello-edu" (same names as drone::make_drone); std::nullopt otherwise.
[[nodiscard]] std::optional<SimModelKind> parse_sim_model(std::string_view name);

struct SimConfig {
    SimModelKind model = SimModelKind::Tello;
    std::string host = "127.0.0.1";
    std::uint16_t command_port = 8889;  // 0 = ephemeral, see Simulator::command_port()
    std::uint16_t state_port = 8890;    // on the client's host; 0 = no telemetry
    double battery = 100.0;             // percent at start
    double drain_per_minute = 8.0;      // in the air; ~12 min flight like the real drone
    std::chrono::milliseconds idle_land{15'000};  // the drone lands after this silence
    std::chrono::milliseconds state_period{100};
    CommandLog on_command;  // optional

    // Failure modes (TEL-00012), all off by default.
    int drop_percent = 0;                     // replies lost (the command still runs)
    std::chrono::milliseconds reply_delay{0};  // before every reply
    std::set<std::string> fail_commands;      // these always answer "error", nothing happens
    unsigned seed = 1;                        // drop pattern, repeatable in tests
};

// Simulated drone model. Error texts follow the real drone where known, otherwise are close.
struct SimModel {
    bool sdk_mode = false;
    bool flying = false;
    double battery = 100.0;
    int height_cm = 0;
    double x_cm = 0.0;  // forward from the takeoff point at yaw 0
    double y_cm = 0.0;  // right
    int yaw = 0;        // -180..180, clockwise positive
    int speed = 10;     // cm/s, set by "speed"
    double flight_s = 0.0;
    Clock::time_point last_command{};
};

// Pure command logic, no sockets: unit-testable with an explicit clock.
class SimLogic {
public:
    explicit SimLogic(const SimConfig& config);

    // Reply text, or std::nullopt when the drone stays silent (before "command", rc).
    std::optional<std::string> handle(std::string_view command, Clock::time_point now);

    // Advance time: battery drain, flight time, auto-land on silence or empty battery.
    void tick(Clock::time_point now);

    [[nodiscard]] const SimModel& model() const { return model_; }
    [[nodiscard]] DroneState state() const;
    [[nodiscard]] std::string telemetry() const;  // one packet as this model sends it

private:
    std::string move(std::string_view direction, int cm);

    SimConfig config_;
    SimModel model_;
    Clock::time_point last_tick_{};
};

// SimLogic behind real UDP sockets: commands on host:command_port, telemetry to state_port.
class Simulator {
public:
    explicit Simulator(SimConfig config);
    ~Simulator();

    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;

    [[nodiscard]] std::uint16_t command_port() const { return command_port_; }
    [[nodiscard]] SimModel model() const;
    [[nodiscard]] int dropped_replies() const { return dropped_.load(); }

private:
    void serve_commands(const std::stop_token& stop);
    void serve_state(const std::stop_token& stop);

    SimConfig config_;
    UdpSocket commands_;
    UdpSocket state_out_;
    std::uint16_t command_port_;
    mutable std::mutex mutex_;
    SimLogic logic_;
    std::string client_host_;
    std::mt19937 random_;           // command thread only
    std::atomic<int> dropped_{0};  // sender of "command": telemetry goes there, like on the real drone
    std::jthread command_thread_;  // threads last: started after everything they use exists
    std::jthread state_thread_;
};

}  // namespace tello::sim
