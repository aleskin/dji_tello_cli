#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace tello {

// The single seam between the SDK command layer and the network: tests replace it with a mock.
class ITransport {
public:
    virtual ~ITransport() = default;

    // Send a command and wait for the reply (trimmed). Throws TelloError on timeout.
    virtual std::string request(std::string_view command, std::chrono::milliseconds timeout) = 0;

    // Fire-and-forget: the drone does not answer some commands (rc).
    virtual void send(std::string_view command) = 0;
};

}  // namespace tello
