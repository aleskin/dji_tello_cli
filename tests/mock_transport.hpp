#pragma once

#include "tello/errors.hpp"
#include "tello/transport.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tello::test
{

// Records everything sent; answers with queued replies, "ok" when the queue is empty.
// A queued std::nullopt simulates a timeout.
class MockTransport final : public ITransport
{
public:
    std::string request(std::string_view command, std::chrono::milliseconds timeout) override
    {
        requests.emplace_back(command);
        last_timeout = timeout;
        if (on_request)
        {
            on_request(command);
        }
        if (replies.empty())
        {
            return "ok";
        }
        const std::optional<std::string> reply = replies.front();
        replies.pop_front();
        if (!reply)
        {
            throw TelloError("timeout waiting for reply to '" + std::string(command) + "'");
        }
        return *reply;
    }

    void send(std::string_view command) override
    {
        sent.emplace_back(command);
    }

    void send_now(std::string_view command) noexcept override
    {
        sent_now.emplace_back(command);
    }

    void reply(std::optional<std::string> r)
    {
        replies.push_back(std::move(r));
    }

    std::vector<std::string> requests; // request(): commands that expect a reply
    std::vector<std::string> sent;     // send(): fire-and-forget commands
    std::vector<std::string> sent_now; // send_now(): crash-handler sends
    std::deque<std::optional<std::string>> replies;
    std::chrono::milliseconds last_timeout{0};
    std::function<void(std::string_view)> on_request; // runs inside request(), before the reply
};

} // namespace tello::test
