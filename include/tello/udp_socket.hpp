#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tello {

struct Datagram {
    std::string data;
    std::string from_host;
    std::uint16_t from_port = 0;
};

// Owning IPv4 UDP socket. Port 0 binds an ephemeral port (see local_port()).
class UdpSocket {
public:
    explicit UdpSocket(const std::string& bind_host = "0.0.0.0", std::uint16_t bind_port = 0);
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    [[nodiscard]] std::uint16_t local_port() const;

    void send_to(std::string_view data, const std::string& host, std::uint16_t port) const;

    // std::nullopt when nothing arrived within the timeout.
    [[nodiscard]] std::optional<Datagram> receive(std::chrono::milliseconds timeout) const;

private:
    int fd_ = -1;
};

}  // namespace tello
