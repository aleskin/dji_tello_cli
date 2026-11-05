#include "cli/command_processor.hpp"

#include "drone/errors.hpp"
#include "tello/version.hpp"

#include "applog/log.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cli
{
namespace
{

using drone::Capability;
using drone::Direction;
using drone::FlipDirection;

// Bad user input: printed as is, the chain stops, nothing was sent.
class UsageError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

std::vector<std::string_view> split(std::string_view s, std::string_view separators)
{
    std::vector<std::string_view> out;
    while (!s.empty())
    {
        const auto start = s.find_first_not_of(separators);
        if (start == std::string_view::npos)
        {
            break;
        }
        s.remove_prefix(start);
        const auto end = s.find_first_of(separators);
        out.push_back(s.substr(0, end));
        s = end == std::string_view::npos ? std::string_view{} : s.substr(end);
    }
    return out;
}

std::string_view trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

int int_arg(std::string_view usage, std::string_view arg)
{
    int value = 0;
    const auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
    if (ec != std::errc() || ptr != arg.data() + arg.size())
    {
        throw UsageError("not a number: '" + std::string(arg) + "', usage: " + std::string(usage));
    }
    return value;
}

FlipDirection flip_arg(std::string_view arg)
{
    if (arg == "l")
    {
        return FlipDirection::Left;
    }
    if (arg == "r")
    {
        return FlipDirection::Right;
    }
    if (arg == "f")
    {
        return FlipDirection::Forward;
    }
    if (arg == "b")
    {
        return FlipDirection::Back;
    }
    throw UsageError("flip direction must be l, r, f or b, got '" + std::string(arg) + "'");
}

} // namespace

std::string_view to_string(Category c)
{
    switch (c)
    {
    case Category::System:
        return "System";
    case Category::Flight:
        return "Flight";
    case Category::Movement:
        return "Movement";
    case Category::Video:
        return "Video";
    case Category::Mission:
        return "Mission";
    case Category::Telemetry:
        return "Telemetry";
    }
    return "?";
}

namespace
{

// Leaves execute(): one level less, also on an exception.
class RunningMark
{
public:
    explicit RunningMark(std::atomic<int>& depth)
        : depth_(depth)
    {
    }
    RunningMark(const RunningMark&) = delete;
    RunningMark& operator=(const RunningMark&) = delete;
    RunningMark(RunningMark&&) = delete;
    RunningMark& operator=(RunningMark&&) = delete;
    ~RunningMark()
    {
        depth_.fetch_sub(1);
    }

private:
    std::atomic<int>& depth_;
};

} // namespace

CommandProcessor::CommandProcessor(drone::DroneBase& drone, std::ostream& out, SessionInfo session,
                                   const tello::StateListener* telemetry, Sleep sleep,
                                   Interrupted interrupted)
    : drone_(drone)
    , out_(out)
    , session_(std::move(session))
    , telemetry_(telemetry)
    , sleep_(sleep ? std::move(sleep)
                   : Sleep(
                         [](std::chrono::milliseconds d)
                         {
                             std::this_thread::sleep_for(d);
                         }))
    , interrupted_(interrupted ? std::move(interrupted)
                               : Interrupted(
                                     []
                                     {
                                         return false;
                                     }))
    , position_(drone_.make_position_source())
{
    sleep_ = [this, inner = std::move(sleep_)](std::chrono::milliseconds d)
    {
        pausing_.store(true);
        try
        {
            inner(d);
        }
        catch (...)
        {
            mark_progress();
            throw;
        }
        mark_progress();
    };
    register_commands();
}

void CommandProcessor::add(CommandInfo info, std::function<void(const Args&)> run)
{
    commands_.push_back(info);
    entries_.push_back(Entry{std::move(info), std::move(run)});
}

void CommandProcessor::register_commands()
{
    using C = Category;
    // System.
    add({"help", {}, C::System, "help [command]", "list commands, or describe one", {}},
        [this](const Args& a)
        {
            print_help(a);
        });
    add({"version", {}, C::System, "version", "program version", {}},
        [this](const Args&)
        {
            out_ << "tello " << tello::version() << '\n';
        });
    add({"log", {}, C::System, "log", "path of this session's log file", {}},
        [this](const Args&)
        {
            const auto path = applog::current();
            out_ << (path.empty() ? std::string("log is off") : path.string()) << '\n';
        });
    add({"info", {}, C::System, "info", "connection, drone model and its readings", {}},
        [this](const Args&)
        {
            print_info();
        });
    add({"wait", {}, C::System, "wait <seconds>", "pause before the next command", {}},
        [this](const Args& a)
        {
            if (a.size() != 1)
            {
                throw UsageError("usage: wait <seconds>");
            }
            double seconds = 0;
            const auto [ptr, ec] = std::from_chars(a[0].data(), a[0].data() + a[0].size(), seconds);
            if (ec != std::errc() || ptr != a[0].data() + a[0].size() || seconds < 0 ||
                !std::isfinite(seconds))
            {
                throw UsageError("usage: wait <seconds>");
            }
            sleep_(std::chrono::milliseconds(std::llround(seconds * 1000)));
        });
    add({"alias",
         {},
         C::System,
         "alias [name [commands]]",
         "list aliases, show one, or define: alias hop up $1; down $1",
         {}},
        [this](const Args& a)
        {
            std::string line;
            for (const auto word : a)
            {
                line += (line.empty() ? "" : " ") + std::string(word);
            }
            alias_command(line);
        });
    add({"aliases", {}, C::System, "aliases", "list aliases", {}},
        [this](const Args&)
        {
            print_aliases();
        });
    add({"unalias", {}, C::System, "unalias <name>", "delete an alias", {}},
        [this](const Args& a)
        {
            unalias_command(a);
        });
    add({"exit", {"quit"}, C::System, "exit", "leave the program", {}},
        [this](const Args&)
        {
            exit_requested_ = true;
        });

    // Flight.
    add({"connect", {}, C::Flight, "connect", "enter SDK mode (done at start)", {}},
        [this](const Args&)
        {
            drone_.connect();
            out_ << "ok\n";
        });
    add({"takeoff",
         {},
         C::Flight,
         "takeoff",
         "take off and hover (~80 cm); then 'up <cm>'",
         Capability::Takeoff},
        [this](const Args&)
        {
            drone_.takeoff();
            airborne_ = true;
            if (position_)
            {
                position_->on_takeoff();
            }
            out_ << "ok\n";
        });
    add({"land", {}, C::Flight, "land", "land", Capability::Land},
        [this](const Args&)
        {
            drone_.land();
            airborne_ = false;
            if (position_)
            {
                position_->on_land();
            }
            out_ << "ok\n";
        });
    add({"hover", {"stop"}, C::Flight, "hover", "stop moving and hold position", Capability::Hover},
        [this](const Args&)
        {
            drone_.hover();
            out_ << "ok\n";
        });
    // The panic button: no confirmation, by design.
    add({"emergency",
         {},
         C::Flight,
         "emergency",
         "MOTORS OFF NOW: the drone falls",
         Capability::Emergency},
        [this](const Args&)
        {
            drone_.emergency();
            airborne_ = false;
            if (position_)
            {
                position_->on_land();
            }
            out_ << "ok\n";
        });

    // Movement.
    const std::array<std::pair<const char*, Direction>, 6> moves{{
        {"forward", Direction::Forward},
        {"back", Direction::Back},
        {"left", Direction::Left},
        {"right", Direction::Right},
        {"up", Direction::Up},
        {"down", Direction::Down},
    }};
    for (const auto& [name, dir] : moves)
    {
        const std::string usage = std::string(name) + " <cm>";
        add({name, {}, C::Movement, usage, "move 20..500 cm", Capability::Move},
            [this, dir = dir, usage](const Args& a)
            {
                if (a.size() != 1)
                {
                    throw UsageError("usage: " + usage);
                }
                const int cm = int_arg(usage, a[0]);
                drone_.move(dir, cm);
                if (position_)
                {
                    position_->on_move(dir, cm);
                }
                out_ << "ok\n";
            });
    }
    add({"cw",
         {"rotate_cw"},
         C::Movement,
         "cw <degrees>",
         "turn clockwise 1..360",
         Capability::Rotate},
        [this](const Args& a)
        {
            if (a.size() != 1)
            {
                throw UsageError("usage: cw <degrees>");
            }
            const int degrees = int_arg("cw <degrees>", a[0]);
            drone_.rotate_cw(degrees);
            if (position_)
            {
                position_->on_rotate(degrees);
            }
            out_ << "ok\n";
        });
    add({"ccw",
         {"rotate_ccw"},
         C::Movement,
         "ccw <degrees>",
         "turn counter-clockwise 1..360",
         Capability::Rotate},
        [this](const Args& a)
        {
            if (a.size() != 1)
            {
                throw UsageError("usage: ccw <degrees>");
            }
            const int degrees = int_arg("ccw <degrees>", a[0]);
            drone_.rotate_ccw(degrees);
            if (position_)
            {
                position_->on_rotate(-degrees);
            }
            out_ << "ok\n";
        });
    add({"flip",
         {},
         C::Movement,
         "flip <l|r|f|b>",
         "flip; needs battery above 50 %",
         Capability::Flip},
        [this](const Args& a)
        {
            if (a.size() != 1)
            {
                throw UsageError("usage: flip <l|r|f|b>");
            }
            drone_.flip(flip_arg(a[0]));
            if (position_)
            {
                position_->on_flip();
            }
            out_ << "ok\n";
        });
    add({"speed", {}, C::Movement, "speed <cm/s>", "set speed 10..100", Capability::SetSpeed},
        [this](const Args& a)
        {
            if (a.size() != 1)
            {
                throw UsageError("usage: speed <cm/s>");
            }
            drone_.set_speed(int_arg("speed <cm/s>", a[0]));
            out_ << "ok\n";
        });
    add({"go",
         {},
         C::Movement,
         "go <x> <y> <z> <speed>",
         "fly to x y z cm (forward, left, up) at speed",
         Capability::GoTo},
        [this](const Args& a)
        {
            const char* usage = "go <x> <y> <z> <speed>";
            if (a.size() != 4)
            {
                throw UsageError(std::string("usage: ") + usage);
            }
            const int x = int_arg(usage, a[0]);
            const int y = int_arg(usage, a[1]);
            const int z = int_arg(usage, a[2]);
            drone_.go(x, y, z, int_arg(usage, a[3]));
            if (position_)
            {
                position_->on_go(x, y, z);
            }
            out_ << "ok\n";
        });
    add({"return",
         {"home"},
         C::Movement,
         "return",
         "fly back above the takeoff point (by the position estimate)",
         Capability::Position},
        [this](const Args&)
        {
            return_home();
        });

    add({"camera_to_center",
         {},
         C::Movement,
         "camera_to_center <x> <y>",
         "turn the camera towards the point x y cm (as 'position' shows it)",
         Capability::Position},
        [this](const Args& a)
        {
            face_point(a, false);
        });
    add({"camera_from_center",
         {},
         C::Movement,
         "camera_from_center <x> <y>",
         "turn the camera away from the point x y cm",
         Capability::Position},
        [this](const Args& a)
        {
            face_point(a, true);
        });
    add({"orbit",
         {"circle"},
         C::Movement,
         "orbit <radius> [cw|ccw] [speed]",
         "fly a circle around the point radius cm ahead, camera to the center (50..500 cm)",
         Capability::Position},
        [this](const Args& a)
        {
            orbit(a);
        });
    for (const auto& spec : figure_specs())
    {
        add({spec.name,
             {},
             C::Mission,
             spec.usage + " [video] [dry]",
             spec.help,
             Capability::Position},
            [this, &spec](const Args& a)
            {
                run_figure(spec, a);
            });
    }

    // Missions.
    add({"mission",
         {},
         C::Mission,
         "mission <file>",
         "run a command file ('#' comments); on error: stop and land",
         {}},
        [this](const Args& a)
        {
            if (a.size() != 1)
            {
                throw UsageError("usage: mission <file>");
            }
            const std::string path(a[0]);
            std::ifstream file(path);
            if (!file)
            {
                throw UsageError("mission: cannot open '" + path + "'");
            }
            if (!run_mission(file, path))
            {
                throw UsageError("mission '" + path + "' did not complete");
            }
        });
    add({"record",
         {},
         C::Mission,
         "record start <file> | stop | status",
         "write the flight into a mission file: commands as typed, keyboard/joystick flights as "
         "go + turn by the position; 'mission <file>' flies it again",
         {}},
        [this](const Args& a)
        {
            record_command(a);
        });
    add({"square",
         {},
         C::Mission,
         "square <cm>",
         "example mission in the air: forward + cw 90, four times",
         Capability::Move},
        [this](const Args& a)
        {
            if (a.size() != 1)
            {
                throw UsageError("usage: square <cm>");
            }
            const int side = int_arg("square <cm>", a[0]);
            std::string chain;
            for (int i = 0; i < 4; ++i)
            {
                chain += "forward " + std::to_string(side) + "; cw 90; ";
            }
            // A plain chain, so it also works as a line of a mission file (missions do not nest).
            if (run_chain(chain) != Outcome::Done)
            {
                if (!in_mission_ && airborne_)
                {
                    out_ << "square: landing\n";
                    execute("land");
                }
                throw UsageError("square did not complete");
            }
        });

    // Video.
    add({"video",
         {},
         C::Video,
         "video start|stop|show|hide|status",
         "record the camera to an MP4 in " + session_.video_dir.string() +
             "; show/hide the live window",
         Capability::Video},
        [this](const Args& a)
        {
            video_command(a);
        });
    add({"photo",
         {},
         C::Video,
         "photo",
         "save the current camera picture as JPEG in " + session_.photo_dir.string(),
         Capability::Video},
        [this](const Args& a)
        {
            if (!a.empty())
            {
                throw UsageError("usage: photo");
            }
            take_photo();
        });
    add({"keyboard",
         {"kb"},
         C::Movement,
         "keyboard",
         "fly with the keys in the video window: WASD move, arrows up/down/turn, Esc back",
         Capability::Rc},
        [this](const Args& a)
        {
            if (!a.empty())
            {
                throw UsageError("usage: keyboard");
            }
            keyboard_mode();
        });
    add({"joystick",
         {"js"},
         C::Movement,
         "joystick",
         "fly with a gamepad (via the video window): sticks move, a takeoff, b land, "
         "back+start emergency, Esc/guide back",
         Capability::Rc},
        [this](const Args& a)
        {
            if (!a.empty())
            {
                throw UsageError("usage: joystick");
            }
            joystick_mode();
        });
    add({"kbspeed",
         {},
         C::Movement,
         "kbspeed [10..100]",
         "keyboard mode stick deflection, percent (default 40)",
         Capability::Rc},
        [this](const Args& a)
        {
            if (a.size() > 1)
            {
                throw UsageError("usage: kbspeed [10..100]");
            }
            if (a.size() == 1)
            {
                const int speed = int_arg("kbspeed [10..100]", a[0]);
                if (speed < 10 || speed > 100)
                {
                    throw UsageError("kbspeed must be 10..100, got " + std::to_string(speed));
                }
                keyboard_speed_ = speed;
            }
            out_ << "keyboard speed " << keyboard_speed_ << '\n';
        });

    // Telemetry: reads, named like the SDK queries without '?' ('battery?' works too).
    auto read_int = [this](const char* name, const char* help, Capability cap,
                           int (drone::DroneBase::*fn)(), const char* unit)
    {
        add({name, {std::string(name) + "?"}, Category::Telemetry, name, help, cap},
            [this, fn, unit](const Args&)
            {
                out_ << (drone_.*fn)() << unit << '\n';
            });
    };
    auto read_text = [this](const char* name, const char* help, Capability cap,
                            std::string (drone::DroneBase::*fn)())
    {
        add({name, {std::string(name) + "?"}, Category::Telemetry, name, help, cap},
            [this, fn](const Args&)
            {
                out_ << (drone_.*fn)() << '\n';
            });
    };
    read_int("battery", "battery, percent", Capability::Battery, &drone::DroneBase::battery, " %");
    read_int("height", "height, cm", Capability::Height, &drone::DroneBase::height_cm, " cm");
    read_int("wifi", "Wi-Fi signal-to-noise ratio", Capability::Wifi, &drone::DroneBase::wifi_snr,
             "");
    read_int("get_speed", "current speed setting, cm/s", Capability::Speed,
             &drone::DroneBase::speed, " cm/s");
    read_text("temp", "temperature range", Capability::Temperature, &drone::DroneBase::temperature);
    read_text("time", "motors-on time", Capability::FlightTime, &drone::DroneBase::flight_time);
    read_text("attitude", "pitch, roll, yaw", Capability::Attitude, &drone::DroneBase::attitude);
    read_text("baro", "barometer altitude, m", Capability::Barometer, &drone::DroneBase::barometer);
    read_text("tof", "distance to the ground (time-of-flight)", Capability::Tof,
              &drone::DroneBase::tof_distance);
    read_text("sdk", "SDK version", Capability::SdkVersion, &drone::DroneBase::sdk_version);
    read_text("sn", "serial number", Capability::SerialNumber, &drone::DroneBase::serial_number);
    add({"position",
         {"pos", "get_position"},
         Category::Telemetry,
         "position",
         "where the drone is from the takeoff point, and how sure",
         Capability::Position},
        [this](const Args&)
        {
            print_position();
        });
    add({"state", {}, Category::Telemetry, "state", "last telemetry packet (--state-port)", {}},
        [this](const Args&)
        {
            print_state();
        });
}

bool CommandProcessor::available(const CommandInfo& info) const
{
    return !info.needs || drone_.supports(*info.needs);
}

const CommandProcessor::Entry* CommandProcessor::find(std::string_view name) const
{
    for (const auto& e : entries_)
    {
        if (e.info.name == name || std::ranges::find(e.info.aliases, name) != e.info.aliases.end())
        {
            return &e;
        }
    }
    return nullptr;
}

void CommandProcessor::mark_progress()
{
    progress_.store(std::chrono::steady_clock::now().time_since_epoch().count());
    pausing_.store(false);
}

std::chrono::steady_clock::duration CommandProcessor::stalled_for() const
{
    if (!airborne_.load() || running_.load() == 0 || pausing_.load())
    {
        return {};
    }
    const auto since = std::chrono::steady_clock::now() -
                       std::chrono::steady_clock::time_point(
                           std::chrono::steady_clock::duration(progress_.load()));
    return std::min(since, drone_.command_idle_for());
}

bool CommandProcessor::land_before_exit()
{
    if (!airborne_)
    {
        return true;
    }
    if (leave_anyway_line_ == line_count_)
    {
        applog::warn("cli", "leaving with the drone in the air (landing failed)");
        out_ << "leaving with the drone in the air\n";
        return true;
    }
    applog::warn("cli", "exit in the air: landing first");
    out_ << "in the air: landing before exit\n";
    if (execute("land"))
    {
        return true;
    }
    exit_requested_ = false;
    leave_anyway_line_ = line_count_ + 1;
    out_ << "landing failed: try 'land' or 'emergency'; 'exit' again leaves anyway\n";
    return false;
}

bool CommandProcessor::execute_line(std::string_view line)
{
    ++line_count_;
    applog::info("cli", "line: " + std::string(line));
    run_chain(line);
    return !exit_requested_;
}

CommandProcessor::Outcome CommandProcessor::run_chain(std::string_view line)
{
    std::size_t pos = 0;
    while (pos < line.size())
    {
        const auto semi = line.find(';', pos);
        const auto rest = line.substr(pos);
        auto command = trim(line.substr(pos, semi == std::string_view::npos ? semi : semi - pos));
        pos = semi == std::string_view::npos ? line.size() : semi + 1;
        if (command == "alias" || command.starts_with("alias ") || command.starts_with("alias\t"))
        {
            // The body of an alias is the rest of the line, ';' included.
            command = trim(rest);
            pos = line.size();
        }
        if (command.empty())
        {
            continue;
        }
        if (interrupted_())
        {
            applog::warn("cli", "Ctrl+C: skipped '" + std::string(trim(rest)) + "'");
            out_ << "interrupted: rest of the line skipped\n";
            return Outcome::Interrupted;
        }
        if (!execute(command))
        {
            return Outcome::Failed;
        }
        if (exit_requested_)
        {
            return Outcome::Exit;
        }
    }
    return Outcome::Done;
}

bool CommandProcessor::run_mission(std::istream& in, std::string_view name)
{
    if (in_mission_)
    {
        out_ << "missions do not nest: '" << name << "' skipped\n";
        return false;
    }
    in_mission_ = true;
    applog::Scope scope("cli", "mission " + std::string(name));
    try
    {
        const bool ok = run_mission_lines(in, name);
        in_mission_ = false;
        if (!ok)
        {
            scope.fail("stopped");
        }
        return ok;
    }
    catch (...)
    {
        in_mission_ = false;
        throw;
    }
}

bool CommandProcessor::run_mission_lines(std::istream& in, std::string_view name)
{

    std::string line;
    for (int number = 1; std::getline(in, line); ++number)
    {
        const std::string_view text = trim(std::string_view(line).substr(0, line.find('#')));
        if (text.empty())
        {
            continue;
        }
        out_ << name << ':' << number << "> " << text << '\n';
        switch (run_chain(text))
        {
        case Outcome::Done:
            continue;
        case Outcome::Exit:
            exit_requested_ = false; // 'exit' in a file ends the mission, not the program
            out_ << name << ": exit at line " << number << '\n';
            return true;
        case Outcome::Interrupted:
            return false; // Ctrl+C: the caller's on_interrupt() lands
        case Outcome::Failed:
            out_ << name << ": stopped at line " << number << '\n';
            if (airborne_)
            {
                out_ << name << ": landing\n";
                execute("land");
            }
            return false;
        }
    }
    return true;
}

void CommandProcessor::on_interrupt()
{
    if (!airborne_)
    {
        applog::info("cli", "Ctrl+C on the ground");
        return;
    }
    applog::warn("cli", "Ctrl+C in flight: landing");
    out_ << "Ctrl+C in flight: landing (type 'exit' to quit once landed)\n";
    execute("land");
}

bool CommandProcessor::execute(std::string_view command)
{
    running_.fetch_add(1);
    mark_progress();
    const RunningMark running(running_);
    auto words = split(command, " \t");
    const std::string_view name = words.front();
    const Args args(words.begin() + 1, words.end());

    const Entry* entry = find(name);
    if (!entry)
    {
        if (const auto alias = aliases_.find(std::string(name)); alias != aliases_.end())
        {
            return run_alias(alias->second, name, args);
        }
        applog::warn("cli", "unknown command '" + std::string(name) + "'");
        out_ << "unknown command '" << name << "', see 'help'\n";
        return false;
    }
    if (!available(entry->info))
    {
        applog::warn("cli", "'" + std::string(name) + "' is not supported by " + drone_.model());
        out_ << "'" << name << "' is not supported by " << drone_.model() << '\n';
        return false;
    }
    applog::Scope scope("cli", "'" + std::string(command) + "'");
    const bool record = recordable(entry->info, args);
    const bool was_airborne = airborne_;
    std::optional<drone::Position> before;
    if (record && position_)
    {
        before = position_->position();
    }
    try
    {
        if (!record)
        {
            entry->run(args);
            log_position(entry->info);
            return true;
        }
        recording_busy_ = true;
        try
        {
            entry->run(args);
        }
        catch (...)
        {
            recording_busy_ = false;
            throw;
        }
        recording_busy_ = false;
        note_recorded(command, entry->info.name, before, was_airborne);
        log_position(entry->info);
        return true;
    }
    catch (const UsageError& e)
    {
        scope.fail(e.what());
        out_ << e.what() << '\n';
    }
    catch (const drone::NotSupported& e)
    {
        scope.fail(e.what());
        out_ << e.what() << '\n';
    }
    catch (const drone::DroneError& e)
    {
        scope.fail(e.what());
        applog::error("cli", std::string(command) + ": " + e.what());
        out_ << "error: " << e.what() << '\n';
    }
    catch (const std::exception& e)
    {
        // Not a drone error (file system, FFmpeg...): the command fails, the program goes on.
        scope.fail(e.what());
        applog::error("cli", std::string(command) + ": unexpected " + e.what());
        out_ << "error: " << e.what() << '\n';
    }
    return false;
}

void CommandProcessor::log_position(const CommandInfo& info) const
{
    if (!position_ || (info.category != Category::Flight && info.category != Category::Movement &&
                       info.category != Category::Mission))
    {
        return;
    }
    const auto p = position_->position();
    std::ostringstream text;
    text << std::fixed << std::setprecision(0) << "position x=" << p.x_cm << " y=" << p.y_cm
         << " z=" << p.z_cm << " heading=" << p.heading_deg << " error=" << p.error_cm
         << (airborne_ ? " (in the air)" : " (landed)");
    applog::info("cli", text.str());
}

std::vector<std::string> CommandProcessor::completions(std::string_view prefix) const
{
    std::vector<std::string> out;
    for (const auto& e : entries_)
    {
        if (!available(e.info))
        {
            continue;
        }
        if (e.info.name.starts_with(prefix))
        {
            out.push_back(e.info.name);
        }
    }
    for (const auto& [name, body] : aliases_)
    {
        if (name.starts_with(prefix))
        {
            out.push_back(name);
        }
    }
    return out;
}

namespace
{

constexpr int kMaxAliasDepth = 10;

bool valid_alias_name(std::string_view name)
{
    return !name.empty() && std::ranges::all_of(name,
                                                [](char c)
                                                {
                                                    return (c >= 'a' && c <= 'z') ||
                                                           (c >= '0' && c <= '9') || c == '_' ||
                                                           c == '-';
                                                });
}

// "$1".."$9" are the arguments, "$*" all of them; a missing one is a usage error.
std::string expand_alias(std::string_view body, std::string_view name,
                         const std::vector<std::string_view>& args)
{
    std::string out;
    std::size_t highest = 0;
    bool uses_all = false;
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        const char next = i + 1 < body.size() ? body[i + 1] : '\0';
        if (body[i] != '$' || !((next >= '1' && next <= '9') || next == '*'))
        {
            out += body[i];
            continue;
        }
        ++i;
        if (next == '*')
        {
            uses_all = true;
            for (std::size_t k = 0; k < args.size(); ++k)
            {
                out += (k == 0 ? "" : " ") + std::string(args[k]);
            }
            continue;
        }
        const auto index = static_cast<std::size_t>(next - '0');
        highest = std::max(highest, index);
        if (index > args.size())
        {
            throw UsageError("alias '" + std::string(name) + "' needs argument $" +
                             std::to_string(index) + ": " + std::string(body));
        }
        out += args[index - 1];
    }
    if (!uses_all && args.size() > highest)
    {
        throw UsageError("alias '" + std::string(name) + "' takes " + std::to_string(highest) +
                         " argument(s), got " + std::to_string(args.size()) + ": " +
                         std::string(body));
    }
    return out;
}

} // namespace

bool CommandProcessor::run_alias(const std::string& body, std::string_view name, const Args& args)
{
    std::string expanded;
    try
    {
        expanded = expand_alias(body, name, args);
    }
    catch (const UsageError& e)
    {
        out_ << e.what() << '\n';
        return false;
    }
    if (alias_depth_ >= kMaxAliasDepth)
    {
        out_ << "alias '" << name << "': too deep (an alias calling itself?)\n";
        return false;
    }
    ++alias_depth_;
    Outcome outcome = Outcome::Failed;
    try
    {
        outcome = run_chain(expanded);
    }
    catch (...)
    {
        --alias_depth_;
        throw;
    }
    --alias_depth_;
    return outcome == Outcome::Done || outcome == Outcome::Exit;
}

void CommandProcessor::alias_command(std::string_view line)
{
    line = trim(line);
    if (line.empty())
    {
        print_aliases();
        return;
    }
    const auto space = line.find_first_of(" \t");
    const std::string name(line.substr(0, space));
    const std::string_view body =
        space == std::string_view::npos ? std::string_view{} : trim(line.substr(space));
    if (body.empty())
    {
        const auto it = aliases_.find(name);
        if (it == aliases_.end())
        {
            throw UsageError("no alias '" + name + "', see 'aliases'");
        }
        out_ << "alias " << it->first << ' ' << it->second << '\n';
        return;
    }
    if (!valid_alias_name(name))
    {
        throw UsageError("alias name: a-z, 0-9, '_' and '-' only, got '" + name + "'");
    }
    if (find(name) != nullptr)
    {
        throw UsageError("'" + name + "' is a command, pick another alias name");
    }
    aliases_[name] = std::string(body);
    save_aliases();
}

void CommandProcessor::unalias_command(const Args& args)
{
    if (args.size() != 1)
    {
        throw UsageError("usage: unalias <name>");
    }
    if (aliases_.erase(std::string(args[0])) == 0)
    {
        throw UsageError("no alias '" + std::string(args[0]) + "', see 'aliases'");
    }
    save_aliases();
}

void CommandProcessor::print_aliases() const
{
    if (aliases_.empty())
    {
        out_ << "no aliases, define one: alias hop up $1; down $1\n";
        return;
    }
    for (const auto& [name, body] : aliases_)
    {
        out_ << "alias " << name << ' ' << body << '\n';
    }
}

void CommandProcessor::load_aliases(const std::filesystem::path& file)
{
    aliases_file_ = file;
    std::ifstream in(file);
    std::string line;
    for (int number = 1; std::getline(in, line); ++number)
    {
        const auto text = trim(line);
        if (text.empty() || text.front() == '#')
        {
            continue;
        }
        const auto space = text.find_first_of(" \t");
        const std::string name(text.substr(0, space));
        const std::string_view body =
            space == std::string_view::npos ? std::string_view{} : trim(text.substr(space));
        if (body.empty() || !valid_alias_name(name) || find(name) != nullptr)
        {
            out_ << file.string() << ':' << number << ": bad alias line skipped\n";
            continue;
        }
        aliases_[name] = std::string(body);
    }
}

void CommandProcessor::save_aliases()
{
    if (aliases_file_.empty())
    {
        return;
    }
    std::error_code ec;
    if (aliases_file_.has_parent_path())
    {
        std::filesystem::create_directories(aliases_file_.parent_path(), ec);
    }
    std::ofstream file(aliases_file_, std::ios::trunc);
    for (const auto& [name, body] : aliases_)
    {
        file << name << ' ' << body << '\n';
    }
    file.close();
    if (!file)
    {
        out_ << "warning: aliases not saved to " << aliases_file_.string() << '\n';
    }
}

void CommandProcessor::print_help(const Args& args) const
{
    if (!args.empty())
    {
        const Entry* e = find(args[0]);
        if (!e || !available(e->info))
        {
            throw UsageError("no such command for " + drone_.model() + ": '" +
                             std::string(args[0]) + "'");
        }
        out_ << e->info.usage << " — " << e->info.help << '\n';
        return;
    }
    out_ << "Commands for " << drone_.model()
         << " (several in one line: 'takeoff; up 50; land'):\n";
    for (const auto cat : {Category::System, Category::Flight, Category::Movement,
                           Category::Mission, Category::Video, Category::Telemetry})
    {
        out_ << '\n' << to_string(cat) << ":\n";
        for (const auto& e : entries_)
        {
            if (e.info.category != cat || !available(e.info))
            {
                continue;
            }
            std::string usage = e.info.usage;
            usage.resize(std::max<std::size_t>(usage.size(), 26), ' ');
            out_ << "  " << usage << e.info.help << '\n';
        }
    }
}

void CommandProcessor::print_info()
{
    out_ << "tello " << tello::version() << '\n'
         << "drone:      " << drone_.model() << '\n'
         << "commands:   " << session_.host << ':' << session_.command_port << " (UDP)\n"
         << "telemetry:  ";
    if (session_.state_port == 0)
    {
        out_ << "off\n";
    }
    else
    {
        out_ << "UDP " << session_.state_port << '\n';
    }
    // Readings are best effort: a disconnected drone must not make 'info' fail.
    auto line = [this](const char* label, Capability cap, auto read)
    {
        if (!drone_.supports(cap))
        {
            return;
        }
        out_ << label;
        try
        {
            out_ << read() << '\n';
        }
        catch (const drone::DroneError& e)
        {
            out_ << "unavailable (" << e.what() << ")\n";
        }
    };
    line("battery:    ", Capability::Battery,
         [this]
         {
             return std::to_string(drone_.battery()) + " %";
         });
    line("wifi snr:   ", Capability::Wifi,
         [this]
         {
             return std::to_string(drone_.wifi_snr());
         });
    line("sdk:        ", Capability::SdkVersion,
         [this]
         {
             return drone_.sdk_version();
         });
    line("serial:     ", Capability::SerialNumber,
         [this]
         {
             return drone_.serial_number();
         });
}

void CommandProcessor::video_command(const Args& args)
{
    const std::string_view what = args.empty() ? "status" : args[0];
    if (args.size() > 1 ||
        (what != "start" && what != "stop" && what != "show" && what != "hide" && what != "status"))
    {
        throw UsageError("usage: video start|stop|show|hide|status");
    }
    if (what == "show")
    {
        show_window();
        return;
    }
    if (what == "hide")
    {
        out_ << (viewer_ && viewer_->is_open() ? "window closed\n" : "no window\n");
        viewer_.reset();
        return;
    }
    if (what == "start")
    {
        ensure_stream();
        const auto path = video::default_recording_path(session_.video_dir);
        video_->start_recording(path);
        out_ << "recording to " << path.string() << " (from the next keyframe)\n";
        return;
    }
    if (what == "stop")
    {
        if (!video_)
        {
            out_ << "video is off\n";
            return;
        }
        viewer_.reset();
        const auto saved = video_->stop_recording();
        const std::string error = video_->status().error;
        video_.reset();
        out_ << (saved.empty() ? std::string("nothing recorded") : "saved " + saved.string())
             << (error.empty() ? "" : " (" + error + ")") << '\n';
        drone_.stop_video(); // after the file is safe: a lost reply must not lose the video
        return;
    }
    if (!video_)
    {
        out_ << "video is off\n";
        return;
    }
    const auto s = video_->status();
    out_ << (s.receiving ? "receiving" : "no video data") << ", " << s.packets << " frames";
    if (s.width > 0)
    {
        out_ << ", " << s.width << 'x' << s.height;
    }
    out_ << '\n';
    if (viewer_ && viewer_->is_open())
    {
        out_ << "window open\n";
    }
    if (!s.recording.empty())
    {
        out_ << "recording to " << s.recording << '\n';
    }
    else if (s.waiting_for_keyframe)
    {
        out_ << "recording starts at the next keyframe\n";
    }
    if (!s.error.empty())
    {
        out_ << "error: " << s.error << '\n';
    }
}

void CommandProcessor::ensure_stream()
{
    if (!video_)
    {
        drone_.start_video();
        video_ = std::make_unique<video::VideoStream>(drone_.video_source());
    }
}

void CommandProcessor::show_window()
{
    if (!viewer_factory_)
    {
        throw UsageError("video show: no window support in this build");
    }
    if (viewer_ && viewer_->is_open())
    {
        out_ << "window already open\n";
        return;
    }
    viewer_.reset();
    const bool started = !video_;
    ensure_stream();
    const video::VideoStream* stream = video_.get(); // outlives the viewer, see "video stop"
    viewer_ = viewer_factory_({[stream](std::int64_t newer_than)
                               {
                                   return stream->latest_picture(newer_than);
                               },
                               [this]
                               {
                                   return osd();
                               }});
    out_ << "window open (close it or 'video hide')"
         << (started ? ", camera stays on: 'video stop'" : "") << '\n';
}

video::Osd CommandProcessor::osd() const
{
    std::string mode = manual_mode_.load();
    const auto snap = telemetry_ ? telemetry_->latest() : std::nullopt;
    if (!snap || std::chrono::steady_clock::now() - snap->received > std::chrono::seconds(2))
    {
        return {{}, {}, {}, mode};
    }
    return {snap->state.bat, snap->state.h, video::heading_from_yaw(snap->state.yaw), mode};
}

RcSticks sticks_from_keys(const std::set<std::string>& pressed, const KeyLayout& layout, int speed)
{
    auto axis = [&pressed, speed](const std::string& plus, const std::string& minus)
    {
        return (pressed.contains(plus) ? speed : 0) - (pressed.contains(minus) ? speed : 0);
    };
    return {axis(layout.right, layout.left), axis(layout.forward, layout.back),
            axis(layout.up, layout.down), axis(layout.turn_right, layout.turn_left)};
}

void CommandProcessor::open_manual_window(std::string_view mode)
{
    if (!viewer_ || !viewer_->is_open())
    {
        show_window();
    }
    if (!viewer_ || !viewer_->is_open())
    {
        throw UsageError(std::string(mode) + ": no window" +
                         (viewer_ ? " (" + viewer_->error() + ")" : std::string()));
    }
}

void CommandProcessor::keyboard_mode()
{
    if (!airborne_)
    {
        throw UsageError("keyboard: take off first");
    }
    open_manual_window("keyboard");
    viewer_->take_key_presses(); // keys typed before the mode started do not count
    const KeyLayout& k = key_layout_;
    out_ << "keyboard mode in the video window: " << k.forward << '/' << k.left << '/' << k.back
         << '/' << k.right << " move, " << k.up << '/' << k.down << " climb, " << k.turn_left << '/'
         << k.turn_right << " turn, Esc back, speed " << keyboard_speed_ << '\n'
         << std::flush;
    manual_mode_.store("KEYBOARD");
    if (position_)
    {
        position_->on_free_flight();
    }
    std::optional<std::chrono::steady_clock::time_point> last_sample;
    std::string reason = "command mode";
    try
    {
        for (;;)
        {
            if (interrupted_())
            {
                reason = "interrupted";
                break;
            }
            if (!viewer_->is_open())
            {
                reason = "window closed, command mode";
                break;
            }
            const auto presses = viewer_->take_key_presses();
            if (std::ranges::find(presses, "escape") != presses.end())
            {
                break;
            }
            const RcSticks s =
                sticks_from_keys(viewer_->pressed_keys(), key_layout_, keyboard_speed_);
            drone_.rc(s.left_right, s.forward_back, s.up_down, s.yaw);
            feed_motion(last_sample);
            sleep_(kKeyboardPeriod);
        }
    }
    catch (...)
    {
        manual_mode_.store("");
        drone_.rc(0, 0, 0, 0);
        throw;
    }
    manual_mode_.store("");
    drone_.rc(0, 0, 0, 0); // hover
    out_ << reason << '\n';
}

namespace
{

int stick(double value, int deadzone)
{
    const double travel = std::min(std::abs(value) * 100.0, 100.0);
    if (travel <= deadzone)
    {
        return 0;
    }
    const auto out = static_cast<int>(std::lround((travel - deadzone) * 100.0 / (100 - deadzone)));
    return value < 0 ? -out : out;
}

} // namespace

RcSticks sticks_from_gamepad(const video::Gamepad& pad, int deadzone)
{
    return {stick(pad.right_x, deadzone), stick(pad.right_y, deadzone), stick(pad.left_y, deadzone),
            stick(pad.left_x, deadzone)};
}

void CommandProcessor::joystick_mode()
{
    open_manual_window("joystick");
    viewer_->take_key_presses();
    viewer_->take_button_presses();
    out_ << "joystick mode: right stick moves, left stick climbs/turns, " << PadButtons::takeoff
         << " takeoff, " << PadButtons::land << " land, " << PadButtons::emergency_1 << '+'
         << PadButtons::emergency_2 << " emergency, Esc in the window or " << PadButtons::leave
         << " back\n"
         << std::flush;
    manual_mode_.store("JOYSTICK");
    bool free_flight = false;
    std::optional<std::string> pad_name;
    std::optional<std::chrono::steady_clock::time_point> last_sample;
    std::string reason = "command mode";
    auto hover = [this]
    {
        if (airborne_)
        {
            drone_.rc(0, 0, 0, 0);
        }
    };
    try
    {
        bool waiting_shown = false;
        for (;;)
        {
            if (interrupted_())
            {
                reason = "interrupted";
                break;
            }
            if (!viewer_->is_open())
            {
                reason = "window closed, command mode";
                break;
            }
            const auto keys = viewer_->take_key_presses();
            if (std::ranges::find(keys, "escape") != keys.end())
            {
                break;
            }
            const auto buttons = viewer_->take_button_presses();
            const auto pad = viewer_->gamepad();
            if (!pad)
            {
                if (pad_name)
                {
                    out_ << "gamepad disconnected" << (airborne_ ? ", hovering" : "") << '\n'
                         << std::flush;
                    pad_name.reset();
                    hover();
                }
                else if (!waiting_shown)
                {
                    out_ << "no gamepad: connect one (Esc in the window: back)\n" << std::flush;
                }
                waiting_shown = true;
                sleep_(kKeyboardPeriod);
                continue;
            }
            if (pad_name != pad->name)
            {
                pad_name = pad->name;
                out_ << "gamepad: " << pad->name << '\n' << std::flush;
            }
            auto pressed = [&buttons](const char* name)
            {
                return std::ranges::find(buttons, name) != buttons.end();
            };
            if (pressed(PadButtons::leave))
            {
                break;
            }
            if ((pressed(PadButtons::emergency_1) || pressed(PadButtons::emergency_2)) &&
                pad->buttons.contains(PadButtons::emergency_1) &&
                pad->buttons.contains(PadButtons::emergency_2))
            {
                execute("emergency");
                free_flight = false;
                continue;
            }
            if (pressed(PadButtons::takeoff) && !airborne_)
            {
                execute("takeoff");
                free_flight = false;
                last_sample.reset();
                continue;
            }
            if (pressed(PadButtons::land) && airborne_)
            {
                hover();
                execute("land");
                free_flight = false;
                continue;
            }
            if (airborne_)
            {
                if (!free_flight && position_)
                {
                    position_->on_free_flight();
                }
                free_flight = true;
                const RcSticks s = sticks_from_gamepad(*pad, joystick_deadzone_);
                drone_.rc(s.left_right, s.forward_back, s.up_down, s.yaw);
                feed_motion(last_sample);
            }
            sleep_(kKeyboardPeriod);
        }
    }
    catch (...)
    {
        manual_mode_.store("");
        hover();
        throw;
    }
    manual_mode_.store("");
    hover();
    out_ << reason << '\n';
}

void CommandProcessor::feed_motion(std::optional<std::chrono::steady_clock::time_point>& last)
{
    const auto snap = position_ && telemetry_ ? telemetry_->latest() : std::nullopt;
    if (!snap || (last && snap->received <= *last))
    {
        return;
    }
    drone::MotionSample sample;
    // Tello speeds are dm/s; taken as forward/right in the body frame.
    sample.forward_cm_s = snap->state.vgx * 10.0;
    sample.right_cm_s = snap->state.vgy * 10.0;
    sample.yaw_deg = snap->state.yaw;
    sample.height_cm = snap->state.h;
    if (last)
    {
        sample.dt = snap->received - *last;
    }
    last = snap->received;
    position_->on_motion(sample);
}

void CommandProcessor::print_position() const
{
    const auto p = position_->position();
    std::string heading = std::to_string(p.heading_deg);
    heading.insert(0, 3 - std::min<std::size_t>(heading.size(), 3), '0');
    out_ << "x " << std::lround(p.x_cm) << "  y " << std::lround(p.y_cm) << "  z "
         << std::lround(p.z_cm) << " cm  heading " << heading << "  ±" << std::lround(p.error_cm)
         << " cm (" << position_->name() << ")\n";
}

namespace
{

constexpr int kMinMoveCm = 20;
constexpr int kMaxMoveCm = 500;

// "forward 500; forward 230": legs of 20..500 cm; under 20 cm cannot be flown.
std::string legs(double cm, const char* positive, const char* negative)
{
    int left = static_cast<int>(std::lround(std::abs(cm)));
    const char* name = cm >= 0 ? positive : negative;
    std::string out;
    while (left >= kMinMoveCm)
    {
        int step = std::min(left, kMaxMoveCm);
        if (left - step > 0 && left - step < kMinMoveCm)
        {
            step = left - kMinMoveCm;
        }
        out += std::string(name) + ' ' + std::to_string(step) + "; ";
        left -= step;
    }
    return out;
}

} // namespace

namespace
{

constexpr double kDegree = std::numbers::pi / 180.0;

// Degrees into -180..180.
double wrap(double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    if (degrees > 180.0)
    {
        degrees -= 360.0;
    }
    else if (degrees < -180.0)
    {
        degrees += 360.0;
    }
    return degrees;
}

// "cw 45" / "ccw 30", empty under one degree.
std::string turn(double degrees)
{
    const auto whole = std::lround(wrap(degrees));
    if (whole == 0)
    {
        return {};
    }
    return (whole > 0 ? "cw " : "ccw ") + std::to_string(std::abs(whole));
}

} // namespace

void CommandProcessor::face_point(const Args& args, bool away)
{
    const char* name = away ? "camera_from_center" : "camera_to_center";
    const std::string usage = std::string(name) + " <x> <y>";
    if (args.size() != 2)
    {
        throw UsageError("usage: " + usage);
    }
    const int x = int_arg(usage, args[0]);
    const int y = int_arg(usage, args[1]);
    if (!airborne_)
    {
        throw UsageError(std::string(name) + ": not in the air");
    }
    const auto p = position_->position();
    const double dx = x - p.x_cm;
    const double dy = y - p.y_cm;
    if (std::hypot(dx, dy) < 1.0)
    {
        throw UsageError(std::string(name) + ": the drone is at that point");
    }
    const double bearing = std::atan2(dy, dx) / kDegree + (away ? 180.0 : 0.0);
    const std::string command = turn(bearing - p.heading_deg);
    if (!command.empty() && run_chain(command) != Outcome::Done)
    {
        throw UsageError(std::string(name) + ": stopped");
    }
    std::string heading = std::to_string(position_->position().heading_deg);
    heading.insert(0, 3 - std::min<std::size_t>(heading.size(), 3), '0');
    out_ << name << ": heading " << heading << '\n';
}

void CommandProcessor::orbit(const Args& args)
{
    const char* usage = "orbit <radius> [cw|ccw] [speed]";
    if (args.empty() || args.size() > 3)
    {
        throw UsageError(std::string("usage: ") + usage);
    }
    const int radius = int_arg(usage, args[0]);
    if (radius < kMinOrbitCm || radius > kMaxOrbitCm)
    {
        throw UsageError("orbit radius must be " + std::to_string(kMinOrbitCm) + ".." +
                         std::to_string(kMaxOrbitCm) + " cm, got " + std::to_string(radius));
    }
    double sign = 1.0;
    if (args.size() > 1)
    {
        if (args[1] != "cw" && args[1] != "ccw")
        {
            throw UsageError(std::string("usage: ") + usage);
        }
        sign = args[1] == "cw" ? 1.0 : -1.0;
    }
    const int speed = args.size() > 2 ? int_arg(usage, args[2]) : kOrbitSpeed;
    if (speed < kMinOrbitSpeed || speed > kMaxOrbitSpeed)
    {
        throw UsageError("orbit speed must be " + std::to_string(kMinOrbitSpeed) + ".." +
                         std::to_string(kMaxOrbitSpeed) + ", got " + std::to_string(speed));
    }
    if (!airborne_)
    {
        throw UsageError("orbit: not in the air");
    }
    // The center is straight ahead; seen from it the drone sits at bearing heading + 180.
    const auto start = position_->position();
    const double cx = start.x_cm + radius * std::cos(start.heading_deg * kDegree);
    const double cy = start.y_cm + radius * std::sin(start.heading_deg * kDegree);
    const double from_center = start.heading_deg + 180.0;
    out_ << "orbit: radius " << radius << " cm around (" << std::lround(cx) << ", "
         << std::lround(cy) << "), " << kOrbitLegs << " legs\n";
    for (int leg = 1; leg <= kOrbitLegs; ++leg)
    {
        const double bearing = from_center + sign * 360.0 * leg / kOrbitLegs;
        const double tx = cx + radius * std::cos(bearing * kDegree);
        const double ty = cy + radius * std::sin(bearing * kDegree);
        // Each leg is aimed from the current estimate, so rounding does not add up.
        const auto now = position_->position();
        const double h = now.heading_deg * kDegree;
        const double dx = tx - now.x_cm;
        const double dy = ty - now.y_cm;
        const auto forward = std::lround(dx * std::cos(h) + dy * std::sin(h));
        const auto left = std::lround(dx * std::sin(h) - dy * std::cos(h));
        std::string chain = "go " + std::to_string(forward) + " " + std::to_string(left) + " 0 " +
                            std::to_string(speed);
        if (const std::string t = turn(bearing + 180.0 - now.heading_deg); !t.empty())
        {
            chain += "; " + t;
        }
        if (run_chain(chain) != Outcome::Done)
        {
            throw UsageError("orbit: stopped after " + std::to_string(leg - 1) + " of " +
                             std::to_string(kOrbitLegs) + " legs");
        }
    }
    const auto end = position_->position();
    out_ << "orbit: done, " << std::lround(std::hypot(end.x_cm - start.x_cm, end.y_cm - start.y_cm))
         << " cm from the start ±" << std::lround(end.error_cm) << " cm\n";
}

namespace
{

// "2026-11-04_10-15-03": the folder name for the photos of one figure.
std::string local_timestamp()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 32> text{};
    const std::size_t n = std::strftime(text.data(), text.size(), "%Y-%m-%d_%H-%M-%S", &local);
    return {text.data(), n};
}

std::string two_digits(int n)
{
    std::string text = std::to_string(n);
    text.insert(0, 2 - std::min<std::size_t>(text.size(), 2), '0');
    return text;
}

} // namespace

void CommandProcessor::print_dry_run(const FigurePlan& plan)
{
    const auto source = drone_.make_position_source();
    if (!source)
    {
        throw UsageError(plan.name + ": the dry run needs a position source");
    }
    const auto e = estimate(plan, *source);
    out_ << plan.name << " (dry run, nothing is sent): " << plan.summary << '\n';
    for (const auto& line : e.lines)
    {
        out_ << "  " << line << '\n';
    }
    out_ << plan.name << ": " << e.commands << " commands, " << plan.photos() << " photos, about "
         << e.duration.count() / 60 << " min " << e.duration.count() % 60 << " s, "
         << static_cast<double>(std::lround(e.distance_cm / 10.0)) / 10.0 << " m, highest "
         << std::lround(e.highest_cm) << " cm, about " << e.battery_percent << " % battery\n";
    if (plan.min_battery > 0)
    {
        out_ << plan.name << ": needs at least " << plan.min_battery << " % at the takeoff\n";
    }
}

void CommandProcessor::run_figure(const FigureSpec& spec, const Args& args)
{
    const std::string usage = find(spec.name)->info.usage;
    bool dry = false;
    bool video = false;
    Args rest;
    for (const auto arg : args)
    {
        if (arg == "dry")
        {
            dry = true;
        }
        else if (arg == "video")
        {
            video = true;
        }
        else
        {
            rest.push_back(arg);
        }
    }
    FigurePlan plan;
    try
    {
        plan = spec.plan(rest, usage);
    }
    catch (const FigureError& e)
    {
        throw UsageError(e.what());
    }
    plan.video = plan.video || video;
    if (plan.video)
    {
        plan.summary += ", video";
    }
    const std::string& name = plan.name;
    if (dry)
    {
        print_dry_run(plan);
        return;
    }
    if (airborne_)
    {
        throw UsageError(name + ": land first, the figure starts with a takeoff");
    }
    if (plan.min_battery > 0)
    {
        const int battery = drone_.battery();
        if (battery < plan.min_battery)
        {
            throw UsageError(name + ": battery " + std::to_string(battery) + " %, needs " +
                             std::to_string(plan.min_battery) + " %");
        }
    }
    const int photos = plan.photos();
    std::filesystem::path dir;
    if (photos > 0 || plan.video)
    {
        if (!drone_.supports(Capability::Video))
        {
            throw UsageError(name + ": needs the camera");
        }
        // The camera first: no picture, no takeoff.
        ensure_stream();
        if (!video_->wait_for_frame(std::chrono::seconds(5)))
        {
            throw UsageError(name + ": no picture from the camera, not taking off");
        }
    }
    if (photos > 0)
    {
        const std::filesystem::path base = session_.photo_dir / (name + '_' + local_timestamp());
        dir = base;
        for (int n = 2; std::filesystem::exists(dir); ++n)
        {
            dir = base.string() + '_' + std::to_string(n);
        }
        try
        {
            std::filesystem::create_directories(dir);
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            throw UsageError(name + ": " + e.what());
        }
    }
    out_ << name << ": " << plan.summary << (photos > 0 ? " -> " + dir.string() : std::string())
         << '\n'
         << std::flush;

    // A recording the user started goes on; the figure's own one ends with the figure.
    const bool record =
        plan.video && video_->status().recording.empty() && !video_->status().waiting_for_keyframe;
    if (record)
    {
        const auto path = video::default_recording_path(session_.video_dir);
        video_->start_recording(path);
        out_ << name << ": recording to " << path.string() << '\n';
    }
    auto finish_video = [this, record, &name]
    {
        if (record)
        {
            const auto saved = video_->stop_recording();
            out_ << name << ": "
                 << (saved.empty() ? std::string("nothing recorded") : "saved " + saved.string())
                 << '\n';
        }
    };
    auto stop = [this, &name, &finish_video](const std::string& what)
    {
        if (airborne_ && !in_mission_)
        {
            out_ << name << ": landing\n";
            execute("land");
        }
        finish_video();
        throw UsageError(name + ": " + what);
    };
    if (!execute("takeoff"))
    {
        stop("takeoff failed");
    }
    int taken = 0;
    int saved = 0;
    std::vector<std::filesystem::path> frames;
    for (const auto& step : plan.steps)
    {
        if (step.kind == FigureStep::Kind::Photo)
        {
            sleep_(kPhotoSettle);
            ++taken;
            try
            {
                const auto file = dir / (two_digits(taken) + ".jpg");
                video_->save_photo(file);
                frames.push_back(file);
                ++saved;
            }
            catch (const std::runtime_error& e)
            {
                out_ << name << ": photo " << taken << " failed: " << e.what() << '\n';
            }
            continue;
        }
        if (step.kind == FigureStep::Kind::Pause)
        {
            sleep_(step.pause);
            continue;
        }
        std::string chain;
        for (const auto& command : step_commands(position_->position(), step, plan.speed))
        {
            chain += command + "; ";
        }
        if (!chain.empty() && run_chain(chain) != Outcome::Done)
        {
            stop(photos == 0       ? std::string("stopped")
                 : taken == 0      ? std::string("stopped before the first photo")
                 : taken == photos ? std::string("stopped on the way back to the center")
                                   : "stopped after photo " + std::to_string(taken) + " of " +
                                         std::to_string(photos));
        }
    }
    if (!execute("land"))
    {
        finish_video();
        throw UsageError(name + ": land failed");
    }
    finish_video();
    if (plan.movie_fps > 0 && frames.size() >= 2)
    {
        const auto movie = dir / (name + ".mp4");
        try
        {
            video::make_movie(frames, movie, plan.movie_fps);
            out_ << name << ": movie " << movie.string() << '\n';
        }
        catch (const std::runtime_error& e)
        {
            out_ << name << ": " << e.what() << '\n';
        }
    }
    out_ << name << ": done";
    if (photos > 0)
    {
        out_ << ", " << saved << " of " << photos << " photos in " << dir.string();
    }
    out_ << '\n';
}

bool CommandProcessor::recordable(const CommandInfo& info, const Args& args) const
{
    if (!recording_ || recording_busy_ || info.name == "record" || info.name == "mission")
    {
        return false;
    }
    if (std::ranges::find(args, "dry") != args.end() ||
        (info.name == "video" && !args.empty() && args[0] == "status"))
    {
        return false;
    }
    return info.category == Category::Flight || info.category == Category::Movement ||
           info.category == Category::Mission || info.category == Category::Video;
}

void CommandProcessor::note_recorded(std::string_view command, const std::string& name,
                                     const std::optional<drone::Position>& before,
                                     bool was_airborne)
{
    if (!recording_ || name == "kbspeed")
    {
        return;
    }
    auto& lines = recording_->lines;
    if (name != "keyboard" && name != "joystick")
    {
        lines.emplace_back(command);
        return;
    }
    if (was_airborne != airborne_)
    {
        lines.emplace_back(airborne_ ? "takeoff" : "land");
        lines.push_back("# " + name + ": the manual flight around the " +
                        (airborne_ ? "takeoff" : "landing") + " is not recorded");
        return;
    }
    if (!airborne_)
    {
        return;
    }
    if (!before)
    {
        lines.push_back("# " + name + ": not recorded, " + drone_.model() +
                        " does not know its position");
        return;
    }
    const auto route = route_commands(*before, position_->position(), kRouteSpeed);
    lines.push_back("# " + name + (route.empty() ? ": stayed in place" : ""));
    lines.insert(lines.end(), route.begin(), route.end());
}

void CommandProcessor::record_command(const Args& args)
{
    const std::string usage = "usage: record start <file> | stop | status";
    const std::string_view what = args.empty() ? std::string_view("status") : args[0];
    if (what == "start")
    {
        if (args.size() != 2)
        {
            throw UsageError(usage);
        }
        if (recording_)
        {
            throw UsageError("record: already recording to " + recording_->file.string());
        }
        if (in_mission_)
        {
            throw UsageError("record: not inside a mission");
        }
        const std::filesystem::path file(args[1]);
        if (!std::ofstream(file, std::ios::app))
        {
            throw UsageError("record: cannot write '" + file.string() + "'");
        }
        recording_ = Recording{file, {}};
        out_ << "recording the flight to " << file.string()
             << (airborne_ ? " (in the air: the file starts mid-flight, without takeoff)" : "")
             << '\n';
        return;
    }
    if (args.size() != 1)
    {
        throw UsageError(usage);
    }
    if (what == "status")
    {
        if (recording_)
        {
            out_ << "recording to " << recording_->file.string() << ", " << recording_->lines.size()
                 << " lines\n";
        }
        else
        {
            out_ << "not recording\n";
        }
        return;
    }
    if (what != "stop")
    {
        throw UsageError(usage);
    }
    if (!recording_)
    {
        throw UsageError("record: not recording");
    }
    const Recording done = std::move(*recording_);
    recording_.reset();
    std::ofstream file(done.file, std::ios::trunc);
    file << "# recorded by tello " << local_timestamp() << ", replay: mission "
         << done.file.string() << '\n';
    for (const auto& line : done.lines)
    {
        file << line << '\n';
    }
    if (!file)
    {
        throw UsageError("record: cannot write '" + done.file.string() + "'");
    }
    out_ << "saved " << done.lines.size() << " lines to " << done.file.string()
         << (airborne_ ? " (the drone is still in the air: no land at the end)" : "") << '\n';
}

void CommandProcessor::return_home()
{
    if (!airborne_)
    {
        throw UsageError("return: not in the air");
    }
    const auto p = position_->position();
    const double rad = p.heading_deg * std::numbers::pi / 180.0;
    const double forward = -p.x_cm * std::cos(rad) - p.y_cm * std::sin(rad);
    const double right = p.x_cm * std::sin(rad) - p.y_cm * std::cos(rad);
    const std::string path = legs(forward, "forward", "back") + legs(right, "right", "left");
    if (path.empty())
    {
        out_ << "return: already above the takeoff point (±" << std::lround(p.error_cm) << " cm)\n";
        return;
    }
    out_ << "return: " << std::lround(std::hypot(p.x_cm, p.y_cm)) << " cm to the takeoff point\n";
    if (run_chain(path) != Outcome::Done)
    {
        throw UsageError("return: stopped");
    }
    const auto now = position_->position();
    out_ << "return: above the takeoff point, " << std::lround(std::hypot(now.x_cm, now.y_cm))
         << " cm off ±" << std::lround(now.error_cm) << " cm; 'land' to land\n";
}

void CommandProcessor::take_photo()
{
    const bool started = !video_;
    ensure_stream();
    // A fresh stream needs SPS/PPS and a keyframe first: about a second on the Tello.
    if (!video_->wait_for_frame(std::chrono::seconds(5)))
    {
        const std::string error = video_->status().error;
        throw UsageError("photo: no picture from the camera" +
                         (error.empty() ? std::string() : " (" + error + ")"));
    }
    const auto path = video::default_photo_path(session_.photo_dir);
    try
    {
        video_->save_photo(path);
    }
    catch (const std::runtime_error& e)
    {
        throw UsageError(e.what());
    }
    out_ << "saved " << path.string() << (started ? " (camera stays on: 'video stop')" : "")
         << '\n';
}

void CommandProcessor::print_state() const
{
    const auto snap = telemetry_ ? telemetry_->latest() : std::nullopt;
    if (!snap)
    {
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

} // namespace cli
