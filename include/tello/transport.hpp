#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace tello
{

// The single seam between the SDK command layer and the network: tests replace it with a mock.
class ITransport
{
public:
    ITransport() = default;
    virtual ~ITransport() = default;
    ITransport(const ITransport&) = delete;
    ITransport& operator=(const ITransport&) = delete;
    ITransport(ITransport&&) = delete;
    ITransport& operator=(ITransport&&) = delete;

    // Send a command and wait for the reply (trimmed). Throws TelloError on timeout.
    virtual std::string request(std::string_view command, std::chrono::milliseconds timeout) = 0;

    // Fire-and-forget: the drone does not answer some commands (rc).
    virtual void send(std::string_view command) = 0;
};

} // namespace tello
