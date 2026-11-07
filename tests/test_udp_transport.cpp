#include "tello/errors.hpp"
#include "tello/udp_socket.hpp"
#include "tello/udp_transport.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace tello
{
namespace
{

using std::chrono::milliseconds;

TEST(UdpTransport, ReturnsTrimmedReplyFromPeer)
{
    UdpSocket peer("127.0.0.1", 0);
    UdpTransport transport("127.0.0.1", peer.local_port());

    std::thread responder(
        [&peer]
        {
            const auto datagram = peer.receive(milliseconds(2000));
            ASSERT_TRUE(datagram);
            EXPECT_EQ(datagram->data, "battery?");
            peer.send_to("87\r\n", datagram->from_host, datagram->from_port);
        });
    EXPECT_EQ(transport.request("battery?", milliseconds(2000)), "87");
    responder.join();
}

TEST(UdpTransport, SilentPeerTimesOut)
{
    UdpSocket peer("127.0.0.1", 0);
    UdpTransport transport("127.0.0.1", peer.local_port());

    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW((void)transport.request("command", milliseconds(100)), drone::NoReply);
    EXPECT_GE(std::chrono::steady_clock::now() - start, milliseconds(100));
}

TEST(UdpTransport, StaleReplyIsNotTakenForTheNextOne)
{
    UdpSocket peer("127.0.0.1", 0);
    UdpTransport transport("127.0.0.1", peer.local_port());

    EXPECT_THROW((void)transport.request("takeoff", milliseconds(50)), drone::NoReply);
    const auto first = peer.receive(milliseconds(1000));
    ASSERT_TRUE(first);
    peer.send_to("ok", first->from_host, first->from_port); // late answer to "takeoff"
    std::this_thread::sleep_for(milliseconds(50));

    std::thread responder(
        [&peer]
        {
            const auto datagram = peer.receive(milliseconds(2000));
            ASSERT_TRUE(datagram);
            peer.send_to("55", datagram->from_host, datagram->from_port);
        });
    EXPECT_EQ(transport.request("battery?", milliseconds(2000)), "55");
    responder.join();
}

// The reply to a timed-out command arrives after the next command went out and that one is lost:
// the transport cannot tell (no ids), it takes the late reply. The CLI checks 'land' by telemetry.
TEST(UdpTransport, LateReplyAfterTheNextSendIsTakenForIt)
{
    UdpSocket peer("127.0.0.1", 0);
    UdpTransport transport("127.0.0.1", peer.local_port());

    EXPECT_THROW((void)transport.request("forward 100", milliseconds(50)), drone::NoReply);
    const auto first = peer.receive(milliseconds(1000));
    ASSERT_TRUE(first);

    std::thread responder(
        [&peer, &first]
        {
            const auto land = peer.receive(milliseconds(2000)); // "land": dropped, never done
            ASSERT_TRUE(land);
            EXPECT_EQ(land->data, "land");
            peer.send_to("ok", first->from_host, first->from_port); // late, for "forward 100"
        });
    EXPECT_EQ(transport.request("land", milliseconds(2000)), "ok");
    responder.join();
}

} // namespace
} // namespace tello
