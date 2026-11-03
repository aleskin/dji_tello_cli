#include "tello/udp_transport.hpp"

#include "tello/errors.hpp"

#include <chrono>
#include <utility>

namespace tello
{
namespace
{

std::string trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

} // namespace

UdpTransport::UdpTransport(std::string host, std::uint16_t port)
    : host_(std::move(host))
    , port_(port)
    , socket_("0.0.0.0", 0)
{
}

std::string UdpTransport::request(std::string_view command, std::chrono::milliseconds timeout)
{
    const std::lock_guard lock(mutex_);

    // A reply that arrived after the previous request timed out would be taken
    // as the answer to this one: drop whatever is already queued.
    while (socket_.receive(std::chrono::milliseconds(0)))
    {
    }

    socket_.send_to(command, host_, port_);

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
        // Round up: a sub-millisecond remainder must still be waited for, not end the wait early.
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0)
        {
            throw TelloError("timeout waiting for reply to '" + std::string(command) + "'");
        }
        auto datagram = socket_.receive(left);
        if (!datagram)
        {
            continue;
        }
        if (datagram->from_port != port_)
        {
            continue; // not the drone's command port
        }
        return trim(datagram->data);
    }
}

void UdpTransport::send(std::string_view command)
{
    const std::lock_guard lock(mutex_);
    socket_.send_to(command, host_, port_);
}

} // namespace tello
