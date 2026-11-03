#include "cli/command_processor.hpp"

#include "drone/errors.hpp"
#include "tello/version.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cli {
namespace {

using drone::Capability;
using drone::Direction;
using drone::FlipDirection;

// Bad user input: printed as is, the chain stops, nothing was sent.
class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::vector<std::string_view> split(std::string_view s, std::string_view separators) {
    std::vector<std::string_view> out;
    while (!s.empty()) {
        const auto start = s.find_first_not_of(separators);
        if (start == std::string_view::npos) {
            break;
        }
        s.remove_prefix(start);
        const auto end = s.find_first_of(separators);
        out.push_back(s.substr(0, end));
        s = end == std::string_view::npos ? std::string_view{} : s.substr(end);
    }
    return out;
}

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

int int_arg(std::string_view usage, std::string_view arg) {
    int value = 0;
    const auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
    if (ec != std::errc() || ptr != arg.data() + arg.size()) {
        throw UsageError("not a number: '" + std::string(arg) + "', usage: " + std::string(usage));
    }
    return value;
}

FlipDirection flip_arg(std::string_view arg) {
    if (arg == "l") return FlipDirection::Left;
    if (arg == "r") return FlipDirection::Right;
    if (arg == "f") return FlipDirection::Forward;
    if (arg == "b") return FlipDirection::Back;
    throw UsageError("flip direction must be l, r, f or b, got '" + std::string(arg) + "'");
}

}  // namespace

std::string_view to_string(Category c) {
    switch (c) {
        case Category::System: return "System";
        case Category::Flight: return "Flight";
        case Category::Movement: return "Movement";
        case Category::Telemetry: return "Telemetry";
    }
    return "?";
}

CommandProcessor::CommandProcessor(drone::DroneBase& drone, std::ostream& out, SessionInfo session,
                                   const tello::StateListener* telemetry, Sleep sleep,
                                   Interrupted interrupted)
    : drone_(drone),
      out_(out),
      session_(std::move(session)),
      telemetry_(telemetry),
      sleep_(sleep ? std::move(sleep)
                   : Sleep([](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); })),
      interrupted_(interrupted ? std::move(interrupted) : Interrupted([] { return false; })) {
    register_commands();
}

void CommandProcessor::add(CommandInfo info, std::function<void(const Args&)> run) {
    commands_.push_back(info);
    entries_.push_back(Entry{std::move(info), std::move(run)});
}

void CommandProcessor::register_commands() {
    using C = Category;
    // System.
    add({"help", {}, C::System, "help [command]", "list commands, or describe one", {}},
        [this](const Args& a) { print_help(a); });
    add({"version", {}, C::System, "version", "program version", {}}, [this](const Args&) {
        out_ << "tello " << tello::version() << '\n';
    });
    add({"info", {}, C::System, "info", "connection, drone model and its readings", {}},
        [this](const Args&) { print_info(); });
    add({"wait", {}, C::System, "wait <seconds>", "pause before the next command", {}},
        [this](const Args& a) {
            if (a.size() != 1) throw UsageError("usage: wait <seconds>");
            double seconds = 0;
            const auto [ptr, ec] = std::from_chars(a[0].data(), a[0].data() + a[0].size(), seconds);
            if (ec != std::errc() || ptr != a[0].data() + a[0].size() || seconds < 0 ||
                !std::isfinite(seconds)) {
                throw UsageError("usage: wait <seconds>");
            }
            sleep_(std::chrono::milliseconds(std::llround(seconds * 1000)));
        });
    add({"exit", {"quit"}, C::System, "exit", "leave the program", {}},
        [this](const Args&) { exit_requested_ = true; });

    // Flight.
    add({"connect", {}, C::Flight, "connect", "enter SDK mode (done at start)", {}},
        [this](const Args&) { drone_.connect(); out_ << "ok\n"; });
    add({"takeoff", {}, C::Flight, "takeoff", "take off and hover (~80 cm); then 'up <cm>'",
         Capability::Takeoff},
        [this](const Args&) {
            drone_.takeoff();
            airborne_ = true;
            out_ << "ok\n";
        });
    add({"land", {}, C::Flight, "land", "land", Capability::Land}, [this](const Args&) {
        drone_.land();
        airborne_ = false;
        out_ << "ok\n";
    });
    add({"hover", {"stop"}, C::Flight, "hover", "stop moving and hold position",
         Capability::Hover},
        [this](const Args&) { drone_.hover(); out_ << "ok\n"; });
    // The panic button: no confirmation, by design.
    add({"emergency", {}, C::Flight, "emergency", "MOTORS OFF NOW: the drone falls",
         Capability::Emergency},
        [this](const Args&) {
            drone_.emergency();
            airborne_ = false;
            out_ << "ok\n";
        });

    // Movement.
    const std::pair<const char*, Direction> moves[] = {
        {"forward", Direction::Forward}, {"back", Direction::Back}, {"left", Direction::Left},
        {"right", Direction::Right},     {"up", Direction::Up},     {"down", Direction::Down},
    };
    for (const auto& [name, dir] : moves) {
        const std::string usage = std::string(name) + " <cm>";
        add({name, {}, C::Movement, usage, "move 20..500 cm", Capability::Move},
            [this, dir = dir, usage](const Args& a) {
                if (a.size() != 1) throw UsageError("usage: " + usage);
                drone_.move(dir, int_arg(usage, a[0]));
                out_ << "ok\n";
            });
    }
    add({"cw", {"rotate_cw"}, C::Movement, "cw <degrees>", "turn clockwise 1..360",
         Capability::Rotate},
        [this](const Args& a) {
            if (a.size() != 1) throw UsageError("usage: cw <degrees>");
            drone_.rotate_cw(int_arg("cw <degrees>", a[0]));
            out_ << "ok\n";
        });
    add({"ccw", {"rotate_ccw"}, C::Movement, "ccw <degrees>", "turn counter-clockwise 1..360",
         Capability::Rotate},
        [this](const Args& a) {
            if (a.size() != 1) throw UsageError("usage: ccw <degrees>");
            drone_.rotate_ccw(int_arg("ccw <degrees>", a[0]));
            out_ << "ok\n";
        });
    add({"flip", {}, C::Movement, "flip <l|r|f|b>", "flip; needs battery above 50 %",
         Capability::Flip},
        [this](const Args& a) {
            if (a.size() != 1) throw UsageError("usage: flip <l|r|f|b>");
            drone_.flip(flip_arg(a[0]));
            out_ << "ok\n";
        });
    add({"speed", {}, C::Movement, "speed <cm/s>", "set speed 10..100", Capability::SetSpeed},
        [this](const Args& a) {
            if (a.size() != 1) throw UsageError("usage: speed <cm/s>");
            drone_.set_speed(int_arg("speed <cm/s>", a[0]));
            out_ << "ok\n";
        });
    add({"go", {}, C::Movement, "go <x> <y> <z> <speed>",
         "fly to x y z cm (forward, left, up) at speed", Capability::GoTo},
        [this](const Args& a) {
            const char* usage = "go <x> <y> <z> <speed>";
            if (a.size() != 4) throw UsageError(std::string("usage: ") + usage);
            drone_.go(int_arg(usage, a[0]), int_arg(usage, a[1]), int_arg(usage, a[2]),
                      int_arg(usage, a[3]));
            out_ << "ok\n";
        });

    // Telemetry: reads, named like the SDK queries without '?' ('battery?' works too).
    auto read_int = [this](const char* name, const char* help, Capability cap,
                           int (drone::DroneBase::*fn)(), const char* unit) {
        add({name, {std::string(name) + "?"}, Category::Telemetry, name, help, cap},
            [this, fn, unit](const Args&) { out_ << (drone_.*fn)() << unit << '\n'; });
    };
    auto read_text = [this](const char* name, const char* help, Capability cap,
                            std::string (drone::DroneBase::*fn)()) {
        add({name, {std::string(name) + "?"}, Category::Telemetry, name, help, cap},
            [this, fn](const Args&) { out_ << (drone_.*fn)() << '\n'; });
    };
    read_int("battery", "battery, percent", Capability::Battery, &drone::DroneBase::battery, " %");
    read_int("height", "height, cm", Capability::Height, &drone::DroneBase::height_cm, " cm");
    read_int("wifi", "Wi-Fi signal-to-noise ratio", Capability::Wifi, &drone::DroneBase::wifi_snr, "");
    read_int("get_speed", "current speed setting, cm/s", Capability::Speed, &drone::DroneBase::speed,
             " cm/s");
    read_text("temp", "temperature range", Capability::Temperature, &drone::DroneBase::temperature);
    read_text("time", "motors-on time", Capability::FlightTime, &drone::DroneBase::flight_time);
    read_text("attitude", "pitch, roll, yaw", Capability::Attitude, &drone::DroneBase::attitude);
    read_text("baro", "barometer altitude, m", Capability::Barometer, &drone::DroneBase::barometer);
    read_text("tof", "distance to the ground (time-of-flight)", Capability::Tof,
              &drone::DroneBase::tof_distance);
    read_text("sdk", "SDK version", Capability::SdkVersion, &drone::DroneBase::sdk_version);
    read_text("sn", "serial number", Capability::SerialNumber, &drone::DroneBase::serial_number);
    add({"state", {}, Category::Telemetry, "state", "last telemetry packet (--state-port)", {}},
        [this](const Args&) { print_state(); });
}

bool CommandProcessor::available(const CommandInfo& info) const {
    return !info.needs || drone_.supports(*info.needs);
}

const CommandProcessor::Entry* CommandProcessor::find(std::string_view name) const {
    for (const auto& e : entries_) {
        if (e.info.name == name ||
            std::find(e.info.aliases.begin(), e.info.aliases.end(), name) != e.info.aliases.end()) {
            return &e;
        }
    }
    return nullptr;
}

bool CommandProcessor::execute_line(std::string_view line) {
    for (const auto part : split(line, ";")) {
        const auto command = trim(part);
        if (command.empty()) {
            continue;
        }
        if (interrupted_()) {
            out_ << "interrupted: rest of the line skipped\n";
            break;
        }
        if (!execute(command) || exit_requested_) {
            break;
        }
    }
    return !exit_requested_;
}

void CommandProcessor::on_interrupt() {
    if (!airborne_) {
        return;
    }
    out_ << "Ctrl+C in flight: landing (type 'exit' to quit once landed)\n";
    execute("land");
}

bool CommandProcessor::execute(std::string_view command) {
    auto words = split(command, " \t");
    const std::string_view name = words.front();
    const Args args(words.begin() + 1, words.end());

    const Entry* entry = find(name);
    if (!entry) {
        out_ << "unknown command '" << name << "', see 'help'\n";
        return false;
    }
    if (!available(entry->info)) {
        out_ << "'" << name << "' is not supported by " << drone_.model() << '\n';
        return false;
    }
    try {
        entry->run(args);
        return true;
    } catch (const UsageError& e) {
        out_ << e.what() << '\n';
    } catch (const drone::NotSupported& e) {
        out_ << e.what() << '\n';
    } catch (const drone::DroneError& e) {
        out_ << "error: " << e.what() << '\n';
    }
    return false;
}

std::vector<std::string> CommandProcessor::completions(std::string_view prefix) const {
    std::vector<std::string> out;
    for (const auto& e : entries_) {
        if (!available(e.info)) {
            continue;
        }
        if (e.info.name.starts_with(prefix)) {
            out.push_back(e.info.name);
        }
    }
    return out;
}

void CommandProcessor::print_help(const Args& args) const {
    if (!args.empty()) {
        const Entry* e = find(args[0]);
        if (!e || !available(e->info)) {
            throw UsageError("no such command for " + drone_.model() + ": '" +
                             std::string(args[0]) + "'");
        }
        out_ << e->info.usage << " — " << e->info.help << '\n';
        return;
    }
    out_ << "Commands for " << drone_.model() << " (several in one line: 'takeoff; up 50; land'):\n";
    for (const auto cat : {Category::System, Category::Flight, Category::Movement,
                           Category::Telemetry}) {
        out_ << '\n' << to_string(cat) << ":\n";
        for (const auto& e : entries_) {
            if (e.info.category != cat || !available(e.info)) {
                continue;
            }
            std::string usage = e.info.usage;
            usage.resize(std::max<std::size_t>(usage.size(), 26), ' ');
            out_ << "  " << usage << e.info.help << '\n';
        }
    }
}

void CommandProcessor::print_info() {
    out_ << "tello " << tello::version() << '\n'
         << "drone:      " << drone_.model() << '\n'
         << "commands:   " << session_.host << ':' << session_.command_port << " (UDP)\n"
         << "telemetry:  ";
    if (session_.state_port == 0) {
        out_ << "off\n";
    } else {
        out_ << "UDP " << session_.state_port << '\n';
    }
    // Readings are best effort: a disconnected drone must not make 'info' fail.
    auto line = [this](const char* label, Capability cap, auto read) {
        if (!drone_.supports(cap)) {
            return;
        }
        out_ << label;
        try {
            out_ << read() << '\n';
        } catch (const drone::DroneError& e) {
            out_ << "unavailable (" << e.what() << ")\n";
        }
    };
    line("battery:    ", Capability::Battery, [this] { return std::to_string(drone_.battery()) + " %"; });
    line("wifi snr:   ", Capability::Wifi, [this] { return std::to_string(drone_.wifi_snr()); });
    line("sdk:        ", Capability::SdkVersion, [this] { return drone_.sdk_version(); });
    line("serial:     ", Capability::SerialNumber, [this] { return drone_.serial_number(); });
}

void CommandProcessor::print_state() const {
    const auto snap = telemetry_ ? telemetry_->latest() : std::nullopt;
    if (!snap) {
        out_ << "no telemetry received yet\n";
        return;
    }
    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - snap->received);
    const auto& s = snap->state;
    out_ << "battery " << s.bat << " %, height " << s.h << " cm, tof " << s.tof << " cm, baro "
         << s.baro << " m\n"
         << "pitch " << s.pitch << ", roll " << s.roll << ", yaw " << s.yaw << " deg\n"
         << "speed x " << s.vgx << ", y " << s.vgy << ", z " << s.vgz << " dm/s\n"
         << "temp " << s.templ << ".." << s.temph << " C, motors on " << s.time << " s\n"
         << "(" << age.count() << " ms ago)\n";
}

}  // namespace cli
