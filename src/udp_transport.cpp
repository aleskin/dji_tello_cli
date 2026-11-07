#include "tello/udp_transport.hpp"

#include "tello/errors.hpp"

#include "applog/log.hpp"

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
    , peer_(UdpSocket::make_peer(host_, port_))
{
}

void UdpTransport::send_now(std::string_view command) noexcept
{
    socket_.send_now(command, peer_);
}

void UdpTransport::flush_repeats_locked()
{
    if (repeats_ > 0)
    {
        applog::debug("drone",
                      "  (" + last_sent_ + " repeated " + std::to_string(repeats_) + " times)");
    }
    repeats_ = 0;
    last_sent_.clear();
}

std::string UdpTransport::request(std::string_view command, std::chrono::milliseconds timeout)
{
    const std::scoped_lock lock(mutex_);
    flush_repeats_locked();

    // A reply that arrived after the previous request timed out would be taken
    // as the answer to this one: drop whatever is already queued.
    while (auto late = socket_.receive(std::chrono::milliseconds(0)))
    {
        applog::warn("drone", "dropped a late reply '" + trim(late->data) + "'");
        unanswered_ = false;
    }

    applog::info("drone", "> " + std::string(command));
    const auto sent = std::chrono::steady_clock::now();
    try
    {
        socket_.send_to(command, host_, port_);
    }
    catch (const TelloError& e)
    {
        applog::error("drone", "send '" + std::string(command) + "' failed: " + e.what());
        throw;
    }

    const auto deadline = sent + timeout;
    for (;;)
    {
        // Round up: a sub-millisecond remainder must still be waited for, not end the wait early.
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0)
        {
            applog::warn("drone", "! no reply to '" + std::string(command) + "' after " +
                                      applog::ms(std::chrono::steady_clock::now() - sent));
            unanswered_ = true;
            throw drone::NoReply("timeout waiting for reply to '" + std::string(command) + "'");
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
        std::string reply = trim(datagram->data);
        if (unanswered_)
        {
            applog::warn("drone", "the previous command got no reply: '" + reply +
                                      "' may be its late answer, not the answer to '" +
                                      std::string(command) + "'");
            unanswered_ = false;
        }
        applog::info("drone", "< " + reply + "  [" + std::string(command) + ", " +
                                  applog::ms(std::chrono::steady_clock::now() - sent) + "]");
        return reply;
    }
}

void UdpTransport::send(std::string_view command)
{
    const std::scoped_lock lock(mutex_);
    if (command == last_sent_)
    {
        ++repeats_;
    }
    else
    {
        flush_repeats_locked();
        last_sent_ = command;
        applog::debug("drone", "> " + last_sent_ + " (no reply)");
    }
    try
    {
        socket_.send_to(command, host_, port_);
    }
    catch (const TelloError& e)
    {
        applog::error("drone", "send '" + std::string(command) + "' failed: " + e.what());
        throw;
    }
}

} // namespace tello
