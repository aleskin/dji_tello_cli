#pragma once

#include "video/viewer.hpp"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tello::test
{

// Stands in for the SDL window: counts live instances, hands out the sources it was given and
// scripted keys. on_poll runs at every take_key_presses() call, before it returns.
class FakeViewer : public video::Viewer
{
public:
    FakeViewer(video::ViewerSources s, int& alive)
        : sources(std::move(s))
        , alive_(alive)
    {
        ++alive_;
    }
    FakeViewer(const FakeViewer&) = delete;
    FakeViewer& operator=(const FakeViewer&) = delete;
    FakeViewer(FakeViewer&&) = delete;
    FakeViewer& operator=(FakeViewer&&) = delete;
    ~FakeViewer() override
    {
        --alive_;
    }

    [[nodiscard]] bool is_open() const override
    {
        return open;
    }
    [[nodiscard]] std::string error() const override
    {
        return {};
    }
    [[nodiscard]] std::set<std::string> pressed_keys() const override
    {
        return pressed;
    }
    std::vector<std::string> take_key_presses() override
    {
        ++polls;
        if (on_poll)
        {
            on_poll(*this);
        }
        return std::exchange(presses, {});
    }
    [[nodiscard]] std::optional<video::Gamepad> gamepad() const override
    {
        return pad;
    }
    std::vector<std::string> take_button_presses() override
    {
        return std::exchange(button_presses, {});
    }

    video::ViewerSources sources;
    bool open = true;
    std::set<std::string> pressed;
    std::vector<std::string> presses;
    std::optional<video::Gamepad> pad;
    std::vector<std::string> button_presses;
    int polls = 0;
    std::function<void(FakeViewer&)> on_poll;

private:
    int& alive_;
};

} // namespace tello::test
