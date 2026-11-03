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
    EXPECT_THROW((void)transport.request("command", milliseconds(100)), TelloError);
    EXPECT_GE(std::chrono::steady_clock::now() - start, milliseconds(100));
}

TEST(UdpTransport, StaleReplyIsNotTakenForTheNextOne)
{
    UdpSocket peer("127.0.0.1", 0);
    UdpTransport transport("127.0.0.1", peer.local_port());

    EXPECT_THROW((void)transport.request("takeoff", milliseconds(50)), TelloError);
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

} // namespace
} // namespace tello
