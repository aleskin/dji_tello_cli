#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

// Session log for post-flight analysis: one text file per run, one record per line,
// written with a single write(2) per record (no buffering: a crash loses nothing).
// Every layer may log; while no file is open, logging costs one atomic load.
namespace applog
{

enum class Level
{
    Debug,
    Info,
    Warn,
    Error
};

inline constexpr int kDefaultKeep = 30;
inline constexpr const char* kFilePrefix = "tello_";
inline constexpr const char* kFileSuffix = ".log";

// Creates dir, opens dir/tello_YYYYMMDD_HHMMSS.log and deletes the oldest tello_*.log files so
// that at most `keep` remain. Throws std::runtime_error when the file cannot be created.
std::filesystem::path open(const std::filesystem::path& dir, int keep = kDefaultKeep);
void close();

// Empty while no log is open.
[[nodiscard]] std::filesystem::path current();

// "2026-11-05 09:24:20.387 INFO  main     drone: > takeoff". Newlines in message are flattened.
void write(Level level, std::string_view component, std::string_view message);

inline void debug(std::string_view component, std::string_view message)
{
    write(Level::Debug, component, message);
}
inline void info(std::string_view component, std::string_view message)
{
    write(Level::Info, component, message);
}
inline void warn(std::string_view component, std::string_view message)
{
    write(Level::Warn, component, message);
}
inline void error(std::string_view component, std::string_view message)
{
    write(Level::Error, component, message);
}

// Name of the calling thread in every record it writes ("main", "guard", "video"...).
void set_thread_name(std::string name);

// Async-signal-safe: notes a fatal signal in the open log (from a signal handler).
void note_signal(int signal) noexcept;

// "1234 ms"
[[nodiscard]] std::string ms(std::chrono::steady_clock::duration d);

// Logs "begin <what>" now and "end <what>: ok|failed: <why>|exception, N ms" when it goes out of
// scope. A "begin" without its "end" in the log means the call hung or the program died in it.
class Scope
{
public:
    Scope(std::string_view component, std::string what);
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

    void fail(std::string_view why);

private:
    std::string component_;
    std::string what_;
    std::string failure_;
    bool failed_ = false;
    int exceptions_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace applog
