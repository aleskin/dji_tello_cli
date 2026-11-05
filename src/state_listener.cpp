#include "tello/state_listener.hpp"

#include "tello/errors.hpp"

#include "applog/log.hpp"

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
    const std::scoped_lock lock(mutex_);
    return latest_;
}

void StateListener::run(const std::stop_token& stop)
{
    applog::set_thread_name("state");
    applog::info("state", "listening for telemetry on port " + std::to_string(port_));
    bool first = true;
    bool receive_failing = false;
    int bad_packets = 0;
    while (!stop.stop_requested())
    {
        std::optional<Datagram> datagram;
        try
        {
            datagram = socket_.receive(std::chrono::milliseconds(100));
            receive_failing = false;
        }
        catch (const TelloError& e)
        {
            if (!receive_failing)
            {
                applog::error("state", std::string("receive failed: ") + e.what());
            }
            receive_failing = true;
            // An exception leaving a thread is std::terminate; telemetry is best effort.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (!datagram)
        {
            continue;
        }
        auto state = parse_state(datagram->data);
        if (!state)
        {
            if (bad_packets++ < 3) // a broken sender would flood the log
            {
                applog::warn("state", "unparsable telemetry from " + datagram->from_host + ": '" +
                                          datagram->data + "'");
            }
            continue;
        }
        if (first)
        {
            first = false;
            applog::info("state",
                         "first telemetry from " + datagram->from_host + ": " + datagram->data);
        }
        {
            const std::scoped_lock lock(mutex_);
            latest_ = StateSnapshot{*state, std::chrono::steady_clock::now()};
        }
    }
}

} // namespace tello
