#include "tello/udp_socket.hpp"

#include "tello/errors.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <utility>

namespace tello
{
namespace
{

[[noreturn]] void throw_errno(const std::string& what)
{
    throw TelloError(what + ": " + std::strerror(errno));
}

sockaddr_in make_addr(const std::string& host, std::uint16_t port)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
    {
        throw TelloError("invalid IPv4 address: " + host);
    }
    return addr;
}

} // namespace

UdpSocket::UdpSocket(const std::string& bind_host, std::uint16_t bind_port)
{
    fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0)
    {
        throw_errno("socket");
    }
    const sockaddr_in addr = make_addr(bind_host, bind_port);
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        const int saved = errno;
        ::close(fd_);
        fd_ = -1;
        errno = saved;
        throw_errno("bind " + bind_host + ":" + std::to_string(bind_port));
    }
}

UdpSocket::~UdpSocket()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : fd_(std::exchange(other.fd_, -1))
{
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
{
    if (this != &other)
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

std::uint16_t UdpSocket::local_port() const
{
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0)
    {
        throw_errno("getsockname");
    }
    return ntohs(addr.sin_port);
}

void UdpSocket::send_to(std::string_view data, const std::string& host, std::uint16_t port) const
{
    const sockaddr_in addr = make_addr(host, port);
    const ssize_t sent = ::sendto(fd_, data.data(), data.size(), 0,
                                  reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (sent < 0)
    {
        throw_errno("sendto " + host + ":" + std::to_string(port));
    }
}

std::optional<Datagram> UdpSocket::receive(std::chrono::milliseconds timeout) const
{
    pollfd pfd{fd_, POLLIN, 0};
    const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (ready < 0)
    {
        if (errno == EINTR)
        {
            return std::nullopt;
        }
        throw_errno("poll");
    }
    if (ready == 0)
    {
        return std::nullopt;
    }

    std::array<char, 2048> buf{};
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    const ssize_t n =
        ::recvfrom(fd_, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n < 0)
    {
        throw_errno("recvfrom");
    }

    std::array<char, INET_ADDRSTRLEN> host{};
    ::inet_ntop(AF_INET, &from.sin_addr, host.data(), host.size());
    return Datagram{std::string(buf.data(), static_cast<std::size_t>(n)), host.data(),
                    ntohs(from.sin_port)};
}

} // namespace tello
