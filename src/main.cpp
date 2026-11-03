// tello: interactive controller. Talks to the drone only through drone::DroneBase.

#include "cli/command_processor.hpp"
#include "drone/errors.hpp"
#include "drone/factory.hpp"
#include "drone/flight_guard.hpp"
#include "tello/state_listener.hpp"
#include "tello/udp_transport.hpp"
#include "tello/version.hpp"

#include <replxx.hxx>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Options {
    drone::ConnectionSettings drone;
    std::uint16_t state_port = tello::kStatePort;
};

void usage() {
    std::cout << "usage: tello [--host IP] [--port N] [--state-port N] [--model M]\n"
                 "  --host        drone address (default " << tello::kDroneHost << ")\n"
                 "  --port        drone command port (default " << tello::kCommandPort << ")\n"
                 "  --state-port  local telemetry port (default " << tello::kStatePort
              << ", 0 = off)\n"
                 "  --model       tello (default) or tello-edu\n"
                 "Against the simulator: make run-sim\n";
}

std::uint16_t parse_port(std::string_view flag, const std::string& value) {
    std::size_t used = 0;
    const unsigned long port = std::stoul(value, &used);
    if (used != value.size() || port > 65535) {
        throw drone::DroneError(std::string(flag) + ": bad port '" + value + "'");
    }
    return static_cast<std::uint16_t>(port);
}

// Returns false when the program should exit right away (help shown).
bool parse_options(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw drone::DroneError(std::string(arg) + " needs a value");
            }
            return argv[++i];
        };
        if (arg == "--host") {
            opt.drone.host = value();
        } else if (arg == "--port") {
            opt.drone.port = parse_port(arg, value());
        } else if (arg == "--state-port") {
            opt.state_port = parse_port(arg, value());
        } else if (arg == "--model") {
            opt.drone.model = value();
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return false;
        } else {
            throw drone::DroneError("unknown option '" + std::string(arg) + "', see --help");
        }
    }
    if (opt.drone.host.empty()) {
        opt.drone.host = tello::kDroneHost;
    }
    if (opt.drone.port == 0) {
        opt.drone.port = tello::kCommandPort;
    }
    return true;
}

std::string history_path() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.tello_history" : std::string();
}

// Only the first word of a command (after the last ';') is completed: a command name.
replxx::Replxx::completions_t complete(const cli::CommandProcessor& processor,
                                       const std::string& input, int& context_len) {
    replxx::Replxx::completions_t out;
    const auto semi = input.find_last_of(';');
    const std::string_view segment =
        std::string_view(input).substr(semi == std::string::npos ? 0 : semi + 1);
    const auto first = segment.find_first_not_of(' ');
    const std::string_view word = first == std::string_view::npos ? "" : segment.substr(first);
    if (word.find(' ') != std::string_view::npos) {
        return out;  // an argument, not a command name
    }
    context_len = static_cast<int>(word.size());
    for (auto& name : processor.completions(word)) {
        out.emplace_back(name);
    }
    return out;
}

// Battery from telemetry, if a packet arrived recently: no extra traffic to the drone.
std::optional<int> telemetry_battery(const tello::StateListener* telemetry) {
    const auto snap = telemetry ? telemetry->latest() : std::nullopt;
    if (!snap || std::chrono::steady_clock::now() - snap->received > std::chrono::seconds(2)) {
        return std::nullopt;
    }
    return snap->state.bat;
}

int run(drone::DroneBase& drone, cli::CommandProcessor& processor,
        const tello::StateListener* telemetry) {
    auto battery = [telemetry] { return telemetry_battery(telemetry); };
    if (!::isatty(STDIN_FILENO)) {
        // Piped input: one command line per input line, no editing.
        std::mutex out_mutex;
        drone::FlightGuard guard(drone, {}, battery, [&out_mutex](const std::string& m) {
            const std::lock_guard lock(out_mutex);
            std::cout << "! " << m << std::endl;
        });
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!processor.execute_line(line)) {
                break;
            }
        }
        return EXIT_SUCCESS;
    }

    replxx::Replxx rx;
    rx.install_window_change_handler();
    const std::string history = history_path();
    if (!history.empty() && std::filesystem::exists(history)) {
        rx.history_load(history);
    }
    // Keepalive and low-battery landing; messages are printed above the prompt.
    drone::FlightGuard guard(drone, {}, battery,
                             [&rx](const std::string& m) { rx.print("! %s\n", m.c_str()); });
    rx.set_max_history_size(1000);
    rx.set_completion_callback([&processor](const std::string& input, int& context_len) {
        return complete(processor, input, context_len);
    });

    for (;;) {
        errno = 0;
        const char* input = rx.input("tello> ");
        if (input == nullptr) {
            if (errno == EAGAIN) {
                continue;  // Ctrl+C on an empty line: new prompt
            }
            break;  // Ctrl+D
        }
        const std::string line(input);
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        rx.history_add(line);
        if (!processor.execute_line(line)) {
            break;
        }
    }
    if (!history.empty()) {
        rx.history_save(history);
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    try {
        if (!parse_options(argc, argv, opt)) {
            return EXIT_SUCCESS;
        }
        auto drone = drone::make_drone(opt.drone);
        std::unique_ptr<tello::StateListener> telemetry;
        if (opt.state_port != 0) {
            telemetry = std::make_unique<tello::StateListener>("0.0.0.0", opt.state_port);
        }
        cli::CommandProcessor processor(*drone, std::cout,
                                        {opt.drone.host, opt.drone.port, opt.state_port},
                                        telemetry.get());

        std::cout << "tello " << tello::version() << " — " << drone->model() << " at "
                  << opt.drone.host << ':' << opt.drone.port << ". 'help' lists commands.\n";
        processor.execute_line("connect");  // SDK mode; on failure 'connect' can be retried
        return run(*drone, processor, telemetry.get());
    } catch (const std::exception& e) {
        std::cerr << "tello: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
