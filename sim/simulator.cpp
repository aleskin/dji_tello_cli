#include "simulator.hpp"

#include "tello/tello_family.hpp"
#include "tello/errors.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <utility>
#include <vector>

namespace tello::sim {
namespace {

constexpr int kTakeoffHeightCm = 80;  // the real drone hovers at ~80 cm after takeoff
constexpr int kMinHeightCm = 20;
constexpr double kMinTakeoffBattery = 10.0;
constexpr double kMinFlipBattery = 50.0;  // the real drone refuses flips below 50 %

std::vector<std::string_view> split(std::string_view s) {
    std::vector<std::string_view> out;
    while (!s.empty()) {
        const auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) {
            break;
        }
        s.remove_prefix(start);
        const auto end = s.find_first_of(" \t\r\n");
        out.push_back(s.substr(0, end));
        s = end == std::string_view::npos ? std::string_view{} : s.substr(end);
    }
    return out;
}

std::optional<int> to_int(std::string_view s) {
    int value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc() || ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return value;
}

bool in_range(std::optional<int> v, int min, int max) { return v && *v >= min && *v <= max; }

int normalize_yaw(int yaw) {
    yaw %= 360;
    if (yaw > 180) {
        yaw -= 360;
    }
    if (yaw <= -180) {
        yaw += 360;
    }
    return yaw;
}

const std::string kOk = "ok";
const std::string kOutOfRange = "out of range";
const std::string kNotFlying = "error Not flying";

}  // namespace

SimLogic::SimLogic(const SimConfig& config) : config_(config) {
    model_.battery = config.battery;
}

std::optional<std::string> SimLogic::handle(std::string_view command, Clock::time_point now) {
    tick(now);

    const auto args = split(command);
    if (args.empty()) {
        return std::nullopt;
    }
    const std::string_view name = args[0];

    if (!model_.sdk_mode) {
        // Outside SDK mode the real drone ignores everything but "command".
        if (name != "command") {
            return std::nullopt;
        }
        model_.sdk_mode = true;
    }
    model_.last_command = now;

    if (name == "command") {
        return kOk;
    }
    if (name == "takeoff") {
        if (model_.flying) {
            return std::string("error Already flying");
        }
        if (model_.battery < kMinTakeoffBattery) {
            return std::string("error Battery low");
        }
        model_.flying = true;
        model_.height_cm = kTakeoffHeightCm;
        return kOk;
    }
    if (name == "land") {
        if (!model_.flying) {
            return kNotFlying;
        }
        model_.flying = false;
        model_.height_cm = 0;
        return kOk;
    }
    if (name == "emergency") {
        model_.flying = false;
        model_.height_cm = 0;
        return kOk;
    }
    if (name == "stop") {
        return kOk;
    }
    if (name == "up" || name == "down" || name == "left" || name == "right" ||
        name == "forward" || name == "back") {
        const auto cm = args.size() == 2 ? to_int(args[1]) : std::nullopt;
        if (!in_range(cm, kMinMoveCm, kMaxMoveCm)) {
            return kOutOfRange;
        }
        if (!model_.flying) {
            return kNotFlying;
        }
        return move(name, *cm);
    }
    if (name == "cw" || name == "ccw") {
        const auto deg = args.size() == 2 ? to_int(args[1]) : std::nullopt;
        if (!in_range(deg, kMinRotateDeg, kMaxRotateDeg)) {
            return kOutOfRange;
        }
        if (!model_.flying) {
            return kNotFlying;
        }
        model_.yaw = normalize_yaw(model_.yaw + (name == "cw" ? *deg : -*deg));
        return kOk;
    }
    if (name == "flip") {
        if (args.size() != 2 || args[1].size() != 1 ||
            std::string_view("lrfb").find(args[1][0]) == std::string_view::npos) {
            return kOutOfRange;
        }
        if (!model_.flying) {
            return kNotFlying;
        }
        if (model_.battery < kMinFlipBattery) {
            return std::string("error Battery low");
        }
        return kOk;
    }
    if (name == "speed") {
        const auto v = args.size() == 2 ? to_int(args[1]) : std::nullopt;
        if (!in_range(v, kMinSpeed, kMaxSpeed)) {
            return kOutOfRange;
        }
        model_.speed = *v;
        return kOk;
    }
    if (name == "go") {
        if (args.size() != 5) {
            return kOutOfRange;
        }
        const auto x = to_int(args[1]);
        const auto y = to_int(args[2]);
        const auto z = to_int(args[3]);
        const auto v = to_int(args[4]);
        if (!in_range(x, -kMaxGoCm, kMaxGoCm) || !in_range(y, -kMaxGoCm, kMaxGoCm) ||
            !in_range(z, -kMaxGoCm, kMaxGoCm) || !in_range(v, kMinSpeed, kMaxSpeed) ||
            (std::abs(*x) < kMinGoSpan && std::abs(*y) < kMinGoSpan && std::abs(*z) < kMinGoSpan)) {
            return kOutOfRange;
        }
        if (!model_.flying) {
            return kNotFlying;
        }
        // go x y z is in the drone's body frame: x forward, y left, z up.
        const double rad = model_.yaw * std::numbers::pi / 180.0;
        model_.x_cm += *x * std::cos(rad) + *y * std::sin(rad);
        model_.y_cm += *x * std::sin(rad) - *y * std::cos(rad);
        model_.height_cm = std::max(kMinHeightCm, model_.height_cm + *z);
        return kOk;
    }
    if (name == "rc") {
        return std::nullopt;  // the real drone never answers rc
    }

    // Read commands.
    if (name == "battery?") {
        return std::to_string(static_cast<int>(std::lround(model_.battery)));
    }
    if (name == "height?") {
        return std::to_string(model_.height_cm / 10) + "dm";
    }
    if (name == "speed?") {
        return std::to_string(model_.speed) + ".0";
    }
    if (name == "time?") {
        return std::to_string(static_cast<int>(model_.flight_s)) + "s";
    }
    if (name == "wifi?") {
        return std::string("90");
    }
    if (name == "sdk?") {
        return std::string("20");
    }
    if (name == "sn?") {
        return std::string("0TQSIMULATOR01");
    }
    if (name == "temp?") {
        return std::string("62~64C");
    }
    if (name == "tof?") {
        return std::to_string(model_.flying ? model_.height_cm * 10 : 100) + "mm";
    }
    if (name == "attitude?") {
        return "pitch:0;roll:0;yaw:" + std::to_string(model_.yaw) + ";";
    }

    return "unknown command: " + std::string(command);
}

std::string SimLogic::move(std::string_view direction, int cm) {
    if (direction == "up") {
        model_.height_cm += cm;
        return kOk;
    }
    if (direction == "down") {
        model_.height_cm = std::max(kMinHeightCm, model_.height_cm - cm);
        return kOk;
    }
    int heading = model_.yaw;  // forward
    if (direction == "back") {
        heading += 180;
    } else if (direction == "right") {
        heading += 90;
    } else if (direction == "left") {
        heading -= 90;
    }
    const double rad = heading * std::numbers::pi / 180.0;
    model_.x_cm += cm * std::cos(rad);
    model_.y_cm += cm * std::sin(rad);
    return kOk;
}

void SimLogic::tick(Clock::time_point now) {
    if (last_tick_ == Clock::time_point{}) {
        last_tick_ = now;
        return;
    }
    const double dt = std::chrono::duration<double>(now - last_tick_).count();
    last_tick_ = now;
    if (!model_.flying || dt <= 0) {
        return;
    }

    model_.flight_s += dt;
    model_.battery = std::max(0.0, model_.battery - config_.drain_per_minute * dt / 60.0);
    const bool silent = now - model_.last_command > config_.idle_land;
    if (model_.battery <= 0.0 || silent) {
        model_.flying = false;
        model_.height_cm = 0;
    }
}

DroneState SimLogic::state() const {
    DroneState s;
    s.yaw = model_.yaw;
    s.templ = 62;
    s.temph = 64;
    s.tof = model_.flying ? model_.height_cm + 10 : 10;
    s.h = model_.height_cm;
    s.bat = static_cast<int>(std::lround(model_.battery));
    s.baro = model_.height_cm / 100.0;
    s.time = static_cast<int>(model_.flight_s);
    s.agz = -1000.0;
    return s;
}

Simulator::Simulator(SimConfig config)
    : config_(std::move(config)),
      commands_(config_.host, config_.command_port),
      state_out_(config_.host, 0),
      command_port_(commands_.local_port()),
      logic_(config_),
      command_thread_([this](const std::stop_token& stop) { serve_commands(stop); }),
      state_thread_([this](const std::stop_token& stop) { serve_state(stop); }) {}

Simulator::~Simulator() {
    command_thread_.request_stop();
    state_thread_.request_stop();
}

SimModel Simulator::model() const {
    const std::lock_guard lock(mutex_);
    return logic_.model();
}

void Simulator::serve_commands(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        try {
            const auto datagram = commands_.receive(std::chrono::milliseconds(50));
            if (!datagram) {
                continue;
            }
            std::optional<std::string> reply;
            {
                const std::lock_guard lock(mutex_);
                reply = logic_.handle(datagram->data, Clock::now());
                if (logic_.model().sdk_mode && client_host_.empty()) {
                    client_host_ = datagram->from_host;
                }
            }
            if (config_.on_command) {
                config_.on_command(datagram->data, reply);
            }
            if (reply) {
                commands_.send_to(*reply, datagram->from_host, datagram->from_port);
            }
        } catch (const TelloError&) {
            // A failed send must not kill the simulator: the client simply times out.
        }
    }
}

void Simulator::serve_state(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        std::this_thread::sleep_for(config_.state_period);
        std::optional<std::string> packet;
        std::string host;
        {
            const std::lock_guard lock(mutex_);
            logic_.tick(Clock::now());
            if (!client_host_.empty() && config_.state_port != 0) {
                packet = format_state(logic_.state());
                host = client_host_;
            }
        }
        if (packet) {
            try {
                state_out_.send_to(*packet, host, config_.state_port);
            } catch (const TelloError&) {
                // Nobody listening yet is fine: UDP telemetry is best effort.
            }
        }
    }
}

}  // namespace tello::sim
