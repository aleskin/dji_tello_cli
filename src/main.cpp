// tello: interactive controller. Talks to the drone only through drone::DroneBase.

#include "applog/log.hpp"
#include "cli/command_processor.hpp"
#include "cli/config.hpp"
#include "cli/flight_log.hpp"
#include "drone/errors.hpp"
#include "drone/factory.hpp"
#include "drone/flight_guard.hpp"
#include "tello/state_listener.hpp"
#include "tello/udp_transport.hpp"
#include "tello/version.hpp"
#include "video/video_window.hpp"

#include <replxx.hxx>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include <cerrno>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

namespace
{

// Set by SIGINT while a command runs (outside the prompt Ctrl+C is a signal, not a key).
// A signal handler can only reach a global; a lock-free atomic is the one type it may touch.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> g_interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free);

extern "C" void on_sigint(int)
{
    g_interrupted.store(true);
}

void install_sigint()
{
    struct sigaction sa{};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
}

// 'wait' that Ctrl+C cuts short.
void interruptible_sleep(std::chrono::milliseconds d)
{
    const auto until = std::chrono::steady_clock::now() + d;
    while (!g_interrupted.load() && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// One input line; Ctrl+C during it skips the rest of the chain, then lands if in the air.
bool run_line(cli::CommandProcessor& processor, const std::string& line)
{
    g_interrupted.store(false);
    const bool go_on = processor.execute_line(line);
    if (g_interrupted.exchange(false))
    {
        processor.on_interrupt();
    }
    return go_on;
}

// What the crash handlers land: set while the session runs. Lock-free atomics, so a signal
// handler may read them.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<drone::DroneBase*> g_drone{nullptr};
std::atomic<const cli::CommandProcessor*> g_processor{nullptr};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
static_assert(std::atomic<drone::DroneBase*>::is_always_lock_free);
static_assert(std::atomic<const cli::CommandProcessor*>::is_always_lock_free);

// Registers the session's drone for emergency_land() for its lifetime.
class EmergencyTarget
{
public:
    EmergencyTarget(drone::DroneBase& drone, const cli::CommandProcessor& processor)
    {
        g_drone.store(&drone);
        g_processor.store(&processor);
    }
    EmergencyTarget(const EmergencyTarget&) = delete;
    EmergencyTarget& operator=(const EmergencyTarget&) = delete;
    EmergencyTarget(EmergencyTarget&&) = delete;
    EmergencyTarget& operator=(EmergencyTarget&&) = delete;
    ~EmergencyTarget()
    {
        g_processor.store(nullptr);
        g_drone.store(nullptr);
    }
};

// The program is dying: if the drone is in the air, send it down (once, async-signal-safe).
void emergency_land() noexcept
{
    const cli::CommandProcessor* processor = g_processor.exchange(nullptr);
    drone::DroneBase* drone = g_drone.load();
    if (processor == nullptr || drone == nullptr || !processor->airborne())
    {
        return;
    }
    applog::note("EMERGENCY: program dying with the drone in the air, sending land");
    drone->land_now();
}

// Fatal signals: note them in the log, land a flying drone, then die the default way
// (core dump, exit status).
extern "C" void on_fatal_signal(int sig)
{
    applog::note_signal(sig);
    emergency_land();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void install_crash_logging()
{
    for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL, SIGTERM, SIGHUP, SIGQUIT})
    {
        struct sigaction sa{};
        sa.sa_handler = on_fatal_signal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;
        sigaction(sig, &sa, nullptr);
    }
    std::set_terminate(
        []
        {
            try
            {
                if (const auto e = std::current_exception())
                {
                    std::rethrow_exception(e);
                }
                applog::error("main", "std::terminate without an exception");
            }
            catch (const std::exception& e)
            {
                applog::error("main",
                              std::string("std::terminate: uncaught exception: ") + e.what());
            }
            catch (...)
            {
                applog::error("main", "std::terminate: uncaught unknown exception");
            }
            emergency_land();
            std::abort();
        });
}

// What the user sees on the console goes to the log too, as "console: <line>".
class TeeBuf : public std::streambuf
{
public:
    explicit TeeBuf(std::streambuf* target)
        : target_(target)
    {
    }
    TeeBuf(const TeeBuf&) = delete;
    TeeBuf& operator=(const TeeBuf&) = delete;
    TeeBuf(TeeBuf&&) = delete;
    TeeBuf& operator=(TeeBuf&&) = delete;
    ~TeeBuf() override
    {
        if (!line_.empty())
        {
            applog::info("console", line_);
        }
    }

protected:
    int_type overflow(int_type ch) override
    {
        if (traits_type::eq_int_type(ch, traits_type::eof()))
        {
            return traits_type::not_eof(ch);
        }
        const char c = traits_type::to_char_type(ch);
        if (traits_type::eq_int_type(target_->sputc(c), traits_type::eof()))
        {
            return traits_type::eof();
        }
        take(c);
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        const std::streamsize written = target_->sputn(s, n);
        for (const char c : std::string_view(s, static_cast<std::size_t>(n)))
        {
            take(c);
        }
        return written;
    }

    int sync() override
    {
        return target_->pubsync();
    }

private:
    static constexpr std::size_t kMaxLine = 4096;

    void take(char c)
    {
        if (c == '\n' || line_.size() >= kMaxLine)
        {
            applog::info("console", line_);
            line_.clear();
        }
        if (c != '\n')
        {
            line_ += c;
        }
    }

    std::streambuf* target_;
    std::string line_;
};

struct Options
{
    drone::ConnectionSettings drone;
    std::uint16_t state_port = tello::kStatePort;
    std::string mission; // run this file and exit instead of the interactive shell
    drone::GuardSettings guard;
    std::filesystem::path log_dir; // empty: no session log
    int log_keep = applog::kDefaultKeep;
};

// ~/.config/tello/config sets the defaults, the flags parsed after it override them.
cli::Config apply_config(Options& opt)
{
    auto loaded = cli::load_config(cli::config_dir() / "config");
    for (const auto& w : loaded.warnings)
    {
        std::cerr << "tello: " << w << '\n';
    }
    const auto& c = loaded.config;
    opt.drone.host = c.host.value_or(opt.drone.host);
    opt.drone.port = c.port.value_or(opt.drone.port);
    opt.state_port = c.state_port.value_or(opt.state_port);
    opt.drone.video_port = c.video_port.value_or(opt.drone.video_port);
    opt.drone.model = c.model.value_or(opt.drone.model);
    opt.guard.warn_battery = c.warn_battery.value_or(opt.guard.warn_battery);
    opt.guard.land_battery = c.land_battery.value_or(opt.guard.land_battery);
    if (c.log_dir)
    {
        const std::string& dir = *c.log_dir;
        const char* home = std::getenv("HOME");
        if (dir == "off")
        {
            opt.log_dir.clear();
        }
        else if (dir.starts_with("~/") && home != nullptr)
        {
            opt.log_dir = std::filesystem::path(home) / dir.substr(2);
        }
        else
        {
            opt.log_dir = dir;
        }
    }
    opt.log_keep = c.log_keep.value_or(opt.log_keep);
    return c;
}

void usage()
{
    std::cout
        << "usage: tello [--host IP] [--port N] [--state-port N] [--video-port N] [--model M]\n"
           "             [--mission FILE] [--log-dir DIR | --no-log]\n"
           "  --host        drone address (default "
        << tello::kDroneHost
        << ")\n"
           "  --port        drone command port (default "
        << tello::kCommandPort
        << ")\n"
           "  --state-port  local telemetry port (default "
        << tello::kStatePort
        << ", 0 = off)\n"
           "  --video-port  local port of the camera stream (default 11111)\n"
           "  --model       tello (default) or tello-edu\n"
           "  --mission     run a command file, then exit (stops and lands on error)\n"
           "  --log-dir     session log directory (default "
        << cli::default_log_dir().string()
        << ")\n"
           "  --no-log      no session log\n"
           "Defaults: ~/.config/tello/config ('key = value'), flags override it.\n"
           "Against the simulator: make run-sim\n";
}

std::uint16_t parse_port(std::string_view flag, const std::string& value)
{
    std::size_t used = 0;
    const unsigned long port = std::stoul(value, &used);
    if (used != value.size() || port > 65535)
    {
        throw drone::DroneError(std::string(flag) + ": bad port '" + value + "'");
    }
    return static_cast<std::uint16_t>(port);
}

// Returns false when the program should exit right away (help shown).
bool parse_options(int argc, char** argv, Options& opt)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::string
        {
            if (i + 1 >= argc)
            {
                throw drone::DroneError(std::string(arg) + " needs a value");
            }
            return argv[++i];
        };
        if (arg == "--host")
        {
            opt.drone.host = value();
        }
        else if (arg == "--port")
        {
            opt.drone.port = parse_port(arg, value());
        }
        else if (arg == "--state-port")
        {
            opt.state_port = parse_port(arg, value());
        }
        else if (arg == "--video-port")
        {
            opt.drone.video_port = parse_port(arg, value());
        }
        else if (arg == "--mission")
        {
            opt.mission = value();
        }
        else if (arg == "--model")
        {
            opt.drone.model = value();
        }
        else if (arg == "--log-dir")
        {
            opt.log_dir = value();
        }
        else if (arg == "--no-log")
        {
            opt.log_dir.clear();
        }
        else if (arg == "--help" || arg == "-h")
        {
            usage();
            return false;
        }
        else
        {
            throw drone::DroneError("unknown option '" + std::string(arg) + "', see --help");
        }
    }
    if (opt.drone.host.empty())
    {
        opt.drone.host = tello::kDroneHost;
    }
    if (opt.drone.port == 0)
    {
        opt.drone.port = tello::kCommandPort;
    }
    return true;
}

// Recordings and photos: ~/Videos/tello, ~/Pictures/tello (created on first use).
std::filesystem::path home_subdir(const char* first, const char* second)
{
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / first / second;
}

std::string history_path()
{
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.tello_history" : std::string();
}

// Only the first word of a command (after the last ';') is completed: a command name.
replxx::Replxx::completions_t complete(const cli::CommandProcessor& processor,
                                       const std::string& input, int& context_len)
{
    replxx::Replxx::completions_t out;
    const auto semi = input.find_last_of(';');
    const std::string_view segment =
        std::string_view(input).substr(semi == std::string::npos ? 0 : semi + 1);
    const auto first = segment.find_first_not_of(' ');
    const std::string_view word = first == std::string_view::npos ? "" : segment.substr(first);
    if (word.find(' ') != std::string_view::npos)
    {
        return out; // an argument, not a command name
    }
    context_len = static_cast<int>(word.size());
    for (auto& name : processor.completions(word))
    {
        out.emplace_back(name);
    }
    return out;
}

// Battery from telemetry, if a packet arrived recently: no extra traffic to the drone.
std::optional<int> telemetry_battery(const tello::StateListener* telemetry)
{
    const auto snap = telemetry ? telemetry->latest() : std::nullopt;
    if (!snap || std::chrono::steady_clock::now() - snap->received > std::chrono::seconds(2))
    {
        return std::nullopt;
    }
    return snap->state.bat;
}

drone::FlightGuard::StallProbe stall_probe(const cli::CommandProcessor& processor)
{
    return [&processor]
    {
        return processor.stalled_for();
    };
}

// --mission: same guard and Ctrl+C rules as the shell. Exit code 0 only if every line succeeded.
int run_mission_file(drone::DroneBase& drone, cli::CommandProcessor& processor,
                     const tello::StateListener* telemetry, const drone::GuardSettings& settings,
                     const std::string& path)
{
    std::ifstream file(path);
    if (!file)
    {
        std::cerr << "tello: cannot open mission '" << path << "'\n";
        return EXIT_FAILURE;
    }
    std::mutex out_mutex;
    drone::FlightGuard guard(
        drone, settings,
        [telemetry]
        {
            return telemetry_battery(telemetry);
        },
        [&out_mutex](const std::string& m)
        {
            const std::scoped_lock lock(out_mutex);
            std::cout << "! " << m << '\n' << std::flush;
        },
        stall_probe(processor));
    g_interrupted.store(false);
    const bool ok = processor.run_mission(file, path);
    if (g_interrupted.exchange(false))
    {
        processor.on_interrupt();
    }
    const bool landed = processor.land_before_exit();
    return ok && landed ? EXIT_SUCCESS : EXIT_FAILURE;
}

int run(drone::DroneBase& drone, cli::CommandProcessor& processor,
        const tello::StateListener* telemetry, const drone::GuardSettings& settings)
{
    auto battery = [telemetry]
    {
        return telemetry_battery(telemetry);
    };
    if (!::isatty(STDIN_FILENO))
    {
        // Piped input: one command line per input line, no editing.
        std::mutex out_mutex;
        drone::FlightGuard guard(
            drone, settings, battery,
            [&out_mutex](const std::string& m)
            {
                const std::scoped_lock lock(out_mutex);
                std::cout << "! " << m << '\n' << std::flush;
            },
            stall_probe(processor));
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!run_line(processor, line))
            {
                break;
            }
        }
        return processor.land_before_exit() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    replxx::Replxx rx;
    rx.install_window_change_handler();
    const std::string history = history_path();
    if (!history.empty() && std::filesystem::exists(history))
    {
        rx.history_load(history);
    }
    // Keepalive and low-battery landing; messages are printed above the prompt.
    drone::FlightGuard guard(
        drone, settings, battery,
        [&rx](const std::string& m)
        {
            rx.print("! %s\n", m.c_str());
        },
        stall_probe(processor));
    rx.set_max_history_size(1000);
    rx.set_completion_callback(
        [&processor](const std::string& input, int& context_len)
        {
            return complete(processor, input, context_len);
        });

    for (;;)
    {
        errno = 0;
        const char* input = rx.input("tello> ");
        if (input == nullptr)
        {
            if (errno == EAGAIN)
            {
                processor.on_interrupt(); // Ctrl+C at the prompt: land if in the air
                continue;
            }
            if (processor.land_before_exit())
            {
                break; // Ctrl+D
            }
            continue;
        }
        const std::string line(input);
        if (line.find_first_not_of(" \t") == std::string::npos)
        {
            continue;
        }
        rx.history_add(line);
        if (!run_line(processor, line) && processor.land_before_exit())
        {
            break;
        }
    }
    if (!history.empty())
    {
        rx.history_save(history);
    }
    return EXIT_SUCCESS;
}

// Session header: enough to tell later what ran, where, and with which settings.
void log_session_start(int argc, char** argv, const Options& opt, const drone::DroneBase& drone)
{
    std::string args;
    for (int i = 0; i < argc; ++i)
    {
        args += (i == 0 ? "" : " ") + std::string(argv[i]);
    }
    applog::info("main", "tello " + std::string(tello::version()) + " session start, pid " +
                             std::to_string(::getpid()));
    applog::info("main", "argv: " + args);
    applog::info("main", "drone " + drone.model() + " at " + opt.drone.host + ':' +
                             std::to_string(opt.drone.port) + ", state port " +
                             std::to_string(opt.state_port) + ", video port " +
                             std::to_string(opt.drone.video_port));
    applog::info("main", "guard: warn battery " + std::to_string(opt.guard.warn_battery) +
                             "%, land battery " + std::to_string(opt.guard.land_battery) + '%');
    applog::info("main", "mode: " + (opt.mission.empty()
                                         ? std::string(::isatty(STDIN_FILENO) ? "interactive"
                                                                              : "piped input")
                                         : "mission " + opt.mission));
}

int run_session(int argc, char** argv, Options& opt)
{
    const cli::Config config = apply_config(opt);
    if (!parse_options(argc, argv, opt))
    {
        return EXIT_SUCCESS;
    }
    if (!opt.log_dir.empty())
    {
        try
        {
            applog::open(opt.log_dir, opt.log_keep);
        }
        catch (const std::exception& e)
        {
            std::cerr << "tello: " << e.what() << " (flying without a log)\n";
        }
    }
    auto drone = drone::make_drone(opt.drone);
    log_session_start(argc, argv, opt, *drone);
    std::unique_ptr<tello::StateListener> telemetry;
    if (opt.state_port != 0)
    {
        telemetry = std::make_unique<tello::StateListener>("0.0.0.0", opt.state_port);
    }
    install_sigint();
    TeeBuf tee(std::cout.rdbuf());
    std::ostream console(&tee);
    cli::CommandProcessor processor(*drone, console,
                                    {opt.drone.host, opt.drone.port, opt.state_port,
                                     home_subdir("Videos", "tello"),
                                     home_subdir("Pictures", "tello")},
                                    telemetry.get(), interruptible_sleep,
                                    []
                                    {
                                        return g_interrupted.load();
                                    });
    processor.set_viewer_factory(
        [](video::ViewerSources sources) -> std::unique_ptr<video::Viewer>
        {
            return std::make_unique<video::VideoWindow>(std::move(sources));
        });
    processor.set_key_layout(config.keys);
    processor.set_keyboard_speed(config.keyboard_speed.value_or(cli::kDefaultKeyboardSpeed));
    processor.set_joystick_deadzone(config.joystick_deadzone.value_or(cli::kDefaultDeadzone));
    processor.load_aliases(cli::config_dir() / "aliases");
    std::unique_ptr<cli::TelemetryLog> telemetry_log;
    if (telemetry && !applog::current().empty())
    {
        telemetry_log = std::make_unique<cli::TelemetryLog>(*telemetry,
                                                            [&processor]
                                                            {
                                                                return processor.airborne();
                                                            });
    }

    console << "tello " << tello::version() << " — " << drone->model() << " at " << opt.drone.host
            << ':' << opt.drone.port << ". 'help' lists commands.\n"
            << std::flush;
    processor.execute_line("connect"); // SDK mode; on failure 'connect' can be retried
    const EmergencyTarget emergency(*drone, processor);
    int code = EXIT_FAILURE;
    try
    {
        code = opt.mission.empty()
                   ? run(*drone, processor, telemetry.get(), opt.guard)
                   : run_mission_file(*drone, processor, telemetry.get(), opt.guard, opt.mission);
    }
    catch (...)
    {
        // A fatal error must not leave the drone flying: land (waiting for the reply), then fail.
        if (processor.airborne())
        {
            applog::error("main", "fatal error in the air: landing before exit");
            console << "tello: fatal error in the air: landing\n" << std::flush;
            processor.land_before_exit();
        }
        throw;
    }
    if (processor.airborne())
    {
        applog::warn("main", "leaving while the drone is still in the air");
    }
    return code;
}

} // namespace

int main(int argc, char** argv)
{
    applog::set_thread_name("main");
    install_crash_logging();
    Options opt;
    opt.log_dir = cli::default_log_dir();
    int code = EXIT_FAILURE;
    try
    {
        code = run_session(argc, argv, opt);
    }
    catch (const std::exception& e)
    {
        applog::error("main", std::string("fatal: ") + e.what());
        std::cerr << "tello: " << e.what() << '\n';
        code = EXIT_FAILURE;
    }
    applog::info("main", "session end, exit code " + std::to_string(code));
    applog::close();
    return code;
}
