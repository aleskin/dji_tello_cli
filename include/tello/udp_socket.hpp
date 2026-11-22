#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tello
{

struct Datagram
{
    std::string data;
    std::string from_host;
    std::uint16_t from_port = 0;
};

// Owning IPv4 UDP socket. Port 0 binds an ephemeral port (see local_port()).
class UdpSocket
{
public:
    explicit UdpSocket(const std::string& bind_host = "0.0.0.0", std::uint16_t bind_port = 0);
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    [[nodiscard]] std::uint16_t local_port() const;

    void send_to(std::string_view data, const std::string& host, std::uint16_t port) const;

    // A destination resolved in advance, for send_now().
    struct Peer
    {
        std::array<std::byte, 16> address{}; // sockaddr_in
    };
    [[nodiscard]] static Peer make_peer(const std::string& host, std::uint16_t port);

    // Async-signal-safe (a crash handler may call it): no allocation, no exceptions, errors
    // ignored.
    void send_now(std::string_view data, const Peer& peer) const noexcept;

    // std::nullopt when nothing arrived within the timeout.
    [[nodiscard]] std::optional<Datagram> receive(std::chrono::milliseconds timeout) const;

private:
    friend std::optional<std::string> route_problem(const std::string& host, std::uint16_t port);
    int fd_ = -1;
};

// Asks the kernel which local address a datagram to host would leave from (connect on a UDP
// socket, nothing is sent). nullopt: host is reachable on its own network, or loopback.
// Otherwise why this PC cannot be on the host's Wi-Fi: no route, or a local address outside the
// host's /24.
[[nodiscard]] std::optional<std::string> route_problem(const std::string& host, std::uint16_t port);

} // namespace tello
