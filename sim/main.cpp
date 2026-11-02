// tello-sim: fake Tello drone on 127.0.0.1 for manual checks (make sim).

#include "simulator.hpp"

#include "tello/errors.hpp"
#include "tello/version.hpp"

#include <pthread.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace {

void usage() {
    std::cout << "usage: tello-sim [--port N] [--state-port N] [--battery PCT] [--drain PCT_PER_MIN]"
                 " [--quiet]\n"
                 "  --port        command port on 127.0.0.1 (default 8889, 0 = ephemeral)\n"
                 "  --state-port  telemetry port on the client's host (default 8890, 0 = off)\n"
                 "  --battery     battery at start, percent (default 100)\n"
                 "  --drain       battery drain in the air, percent per minute (default 8;\n"
                 "                raise it to test low-battery handling quickly)\n"
                 "  --quiet       do not print commands and replies\n";
}

std::uint16_t parse_port(std::string_view flag, const std::string& value) {
    const unsigned long port = std::stoul(value);
    if (port > 65535) {
        throw tello::TelloError(std::string(flag) + ": port out of range: " + value);
    }
    return static_cast<std::uint16_t>(port);
}

}  // namespace

int main(int argc, char** argv) {
    tello::sim::SimConfig config;
    bool quiet = false;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) {
                    throw tello::TelloError(std::string(arg) + " needs a value");
                }
                return argv[++i];
            };
            if (arg == "--port") {
                config.command_port = parse_port(arg, value());
            } else if (arg == "--state-port") {
                config.state_port = parse_port(arg, value());
            } else if (arg == "--battery") {
                config.battery = std::stod(value());
            } else if (arg == "--drain") {
                config.drain_per_minute = std::stod(value());
            } else if (arg == "--quiet") {
                quiet = true;
            } else if (arg == "--help" || arg == "-h") {
                usage();
                return EXIT_SUCCESS;
            } else {
                usage();
                return EXIT_FAILURE;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "tello-sim: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    std::mutex out_mutex;
    if (!quiet) {
        config.on_command = [&out_mutex](std::string_view command,
                                         const std::optional<std::string>& reply) {
            const std::lock_guard lock(out_mutex);
            std::cout << "<- " << command << "\n-> " << (reply ? *reply : "(no reply)") << std::endl;
        };
    }

    // Block the stop signals before the simulator threads start, so only sigwait sees them.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    try {
        tello::sim::Simulator sim(config);
        std::cout << "tello-sim " << tello::version() << " on 127.0.0.1:" << sim.command_port()
                  << ", telemetry to client port " << config.state_port << ", Ctrl+C to stop"
                  << std::endl;
        int sig = 0;
        sigwait(&signals, &sig);
    } catch (const std::exception& e) {
        std::cerr << "tello-sim: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
