#pragma once

#include "tello/transport.hpp"
#include "tello/udp_socket.hpp"

#include <cstdint>
#include <mutex>
#include <string>

namespace tello {

inline constexpr const char* kDroneHost = "192.168.10.1";
inline constexpr std::uint16_t kCommandPort = 8889;
inline constexpr std::uint16_t kStatePort = 8890;

// Commands over UDP. The drone answers to the sender's address, so the local port is ephemeral.
class UdpTransport final : public ITransport {
public:
    UdpTransport(std::string host, std::uint16_t port);

    std::string request(std::string_view command, std::chrono::milliseconds timeout) override;
    void send(std::string_view command) override;

private:
    std::string host_;
    std::uint16_t port_;
    UdpSocket socket_;
    std::mutex mutex_;  // one request in flight: replies carry no id
};

}  // namespace tello
