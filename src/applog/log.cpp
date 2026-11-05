#include "applog/log.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace applog
{
namespace
{

// The file descriptor is read by note_signal() from a signal handler: it must be a lock-free
// atomic with static storage, constant-initialized (no function-local static).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
constinit std::atomic<int> g_fd{-1};
static_assert(std::atomic<int>::is_always_lock_free);

struct State
{
    std::mutex mutex;
    std::filesystem::path path;
};

State& state()
{
    static State s;
    return s;
}

std::string& thread_name()
{
    thread_local std::string name;
    if (name.empty())
    {
        static std::atomic<int> next{1};
        name = "t" + std::to_string(next.fetch_add(1));
    }
    return name;
}

std::string_view level_name(Level level)
{
    switch (level)
    {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO ";
    case Level::Warn:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    }
    return "?    ";
}

// "2026-11-05 09:24:20.387" (local time) or, with compact, "20261105_092420".
std::string timestamp(bool compact)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::array<char, 32> buf{};
    if (compact)
    {
        std::strftime(buf.data(), buf.size(), "%Y%m%d_%H%M%S", &tm);
        return buf.data();
    }
    std::strftime(buf.data(), buf.size(), "%Y-%m-%d %H:%M:%S", &tm);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    std::array<char, 8> frac{};
    std::snprintf(frac.data(), frac.size(), ".%03d", static_cast<int>(millis));
    return std::string(buf.data()) + frac.data();
}

bool is_log_file(const std::filesystem::path& p)
{
    const std::string name = p.filename().string();
    return name.starts_with(kFilePrefix) && name.ends_with(kFileSuffix);
}

// Oldest first: the names carry the start time. The file just opened always stays.
void rotate(const std::filesystem::path& dir, const std::filesystem::path& current, int keep)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec) && is_log_file(entry.path()) && entry.path() != current)
        {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    const auto excess = static_cast<std::ptrdiff_t>(files.size()) - (std::max(keep, 1) - 1);
    for (std::ptrdiff_t i = 0; i < excess; ++i)
    {
        std::filesystem::remove(files[static_cast<std::size_t>(i)], ec);
    }
}

void write_all(int fd, std::string_view text)
{
    while (!text.empty())
    {
        const ssize_t n = ::write(fd, text.data(), text.size());
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return; // a full disk must not take the flight down
        }
        text.remove_prefix(static_cast<std::size_t>(n));
    }
}

} // namespace

std::filesystem::path open(const std::filesystem::path& dir, int keep)
{
    close();
    std::filesystem::create_directories(dir);
    const std::string stamp = timestamp(true);
    for (int n = 1; n < 100; ++n)
    {
        const std::string suffix = n == 1 ? "" : "_" + std::to_string(n);
        std::string name = kFilePrefix;
        name += stamp;
        name += suffix;
        name += kFileSuffix;
        const auto path = dir / name;
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC,
                              0644); // NOLINT(cppcoreguidelines-pro-type-vararg)
        if (fd < 0)
        {
            if (errno == EEXIST)
            {
                continue; // two sessions in one second
            }
            throw std::runtime_error("cannot create log " + path.string() + ": " +
                                     std::strerror(errno));
        }
        {
            const std::scoped_lock lock(state().mutex);
            state().path = path;
            g_fd.store(fd);
        }
        rotate(dir, path, keep);
        return path;
    }
    throw std::runtime_error("cannot create a log in " + dir.string());
}

void close()
{
    const std::scoped_lock lock(state().mutex);
    const int fd = g_fd.exchange(-1);
    if (fd >= 0)
    {
        ::close(fd);
    }
    state().path.clear();
}

std::filesystem::path current()
{
    const std::scoped_lock lock(state().mutex);
    return state().path;
}

void write(Level level, std::string_view component, std::string_view message)
{
    if (g_fd.load() < 0)
    {
        return;
    }
    std::string line = timestamp(false);
    line += ' ';
    line += level_name(level);
    line += ' ';
    std::string name = thread_name();
    name.resize(std::max<std::size_t>(name.size(), 8), ' ');
    line += name;
    line += ' ';
    line += component;
    line += ": ";
    for (const char c : message)
    {
        if (c == '\n')
        {
            line += " | ";
        }
        else if (c != '\r')
        {
            line += c;
        }
    }
    while (line.ends_with(" | "))
    {
        line.resize(line.size() - 3);
    }
    line += '\n';
    const std::scoped_lock lock(state().mutex);
    const int fd = g_fd.load();
    if (fd >= 0)
    {
        write_all(fd, line);
    }
}

void set_thread_name(std::string name)
{
    thread_name() = std::move(name);
}

void note_signal(int signal) noexcept
{
    const int fd = g_fd.load();
    if (fd < 0)
    {
        return;
    }
    // Only async-signal-safe calls here: no allocation, no locks, no stdio.
    std::array<char, 64> text{};
    const char* head = "*** FATAL signal ";
    std::size_t n = 0;
    for (const char* p = head; *p != '\0'; ++p)
    {
        text.at(n++) = *p;
    }
    std::array<char, 12> digits{};
    std::size_t d = 0;
    unsigned value = signal < 0 ? 0U : static_cast<unsigned>(signal);
    for (bool first = true; (first || value != 0) && d < digits.size(); first = false)
    {
        digits.at(d++) = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (d > 0)
    {
        text.at(n++) = digits.at(--d);
    }
    text.at(n++) = '\n';
    [[maybe_unused]] const ssize_t written = ::write(fd, text.data(), n);
}

void note(const char* text) noexcept
{
    const int fd = g_fd.load();
    if (fd < 0 || text == nullptr)
    {
        return;
    }
    std::array<char, 256> line{};
    std::size_t n = 0;
    for (const char* p = "*** "; *p != '\0'; ++p)
    {
        line.at(n++) = *p;
    }
    for (const char* p = text; *p != '\0' && n + 1 < line.size(); ++p)
    {
        line.at(n++) = *p;
    }
    line.at(n++) = '\n';
    [[maybe_unused]] const ssize_t written = ::write(fd, line.data(), n);
}

std::string ms(std::chrono::steady_clock::duration d)
{
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(d).count()) + " ms";
}

Scope::Scope(std::string_view component, std::string what)
    : component_(component)
    , what_(std::move(what))
    , exceptions_(std::uncaught_exceptions())
    , start_(std::chrono::steady_clock::now())
{
    write(Level::Debug, component_, "begin " + what_);
}

Scope::~Scope()
{
    const std::string took = ms(std::chrono::steady_clock::now() - start_);
    try
    {
        if (std::uncaught_exceptions() > exceptions_)
        {
            write(Level::Error, component_, "end " + what_ + ": exception, " + took);
        }
        else if (failed_)
        {
            write(Level::Warn, component_, "end " + what_ + ": failed: " + failure_ + ", " + took);
        }
        else
        {
            write(Level::Info, component_, "end " + what_ + ": ok, " + took);
        }
    }
    catch (...) // NOLINT(bugprone-empty-catch): a destructor must not throw
    {
    }
}

void Scope::fail(std::string_view why)
{
    failed_ = true;
    failure_ = why;
}

} // namespace applog
