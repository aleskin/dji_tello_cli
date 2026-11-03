#pragma once

#include "tello/state.hpp"
#include "tello/udp_socket.hpp"
#include "tello/udp_transport.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace tello
{

struct StateSnapshot
{
    DroneState state;
    std::chrono::steady_clock::time_point received;
};

// Receives telemetry in a background thread and keeps the latest valid packet.
// Port 0 binds an ephemeral port (tests), see local_port().
class StateListener
{
public:
    explicit StateListener(const std::string& bind_host = "0.0.0.0",
                           std::uint16_t port = kStatePort);
    ~StateListener();

    StateListener(const StateListener&) = delete;
    StateListener& operator=(const StateListener&) = delete;

    [[nodiscard]] std::uint16_t local_port() const
    {
        return port_;
    }
    [[nodiscard]] std::optional<StateSnapshot> latest() const;

private:
    void run(const std::stop_token& stop);

    UdpSocket socket_;
    std::uint16_t port_;
    mutable std::mutex mutex_;
    std::optional<StateSnapshot> latest_;
    std::jthread thread_; // last member: started after everything it uses exists
};

} // namespace tello
