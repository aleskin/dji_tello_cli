#include "tello/state_listener.hpp"

#include "tello/errors.hpp"

namespace tello
{

StateListener::StateListener(const std::string& bind_host, std::uint16_t port)
    : socket_(bind_host, port)
    , port_(socket_.local_port())
    , thread_(
          [this](const std::stop_token& stop)
          {
              run(stop);
          })
{
}

StateListener::~StateListener()
{
    thread_.request_stop();
    // jthread joins in its destructor; the poll timeout below bounds the wait.
}

std::optional<StateSnapshot> StateListener::latest() const
{
    const std::lock_guard lock(mutex_);
    return latest_;
}

void StateListener::run(const std::stop_token& stop)
{
    while (!stop.stop_requested())
    {
        std::optional<Datagram> datagram;
        try
        {
            datagram = socket_.receive(std::chrono::milliseconds(100));
        }
        catch (const TelloError&)
        {
            // An exception leaving a thread is std::terminate; telemetry is best effort.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (!datagram)
        {
            continue;
        }
        if (auto state = parse_state(datagram->data))
        {
            const std::lock_guard lock(mutex_);
            latest_ = StateSnapshot{*state, std::chrono::steady_clock::now()};
        }
    }
}

} // namespace tello
