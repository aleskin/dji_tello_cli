#pragma once

#include "drone/drone_base.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace drone
{

struct ConnectionSettings
{
    std::string model = "tello"; // see known_models()
    std::string host;            // empty = the model's default address
    std::uint16_t port = 0;      // 0 = the model's default command port
    std::chrono::milliseconds action_timeout{20'000};
    std::chrono::milliseconds query_timeout{3'000};
};

// Model names accepted by make_drone(), e.g. "tello", "tello-edu".
[[nodiscard]] std::vector<std::string_view> known_models();

// The only place that knows concrete models: everything above works through DroneBase.
// Throws DroneError for an unknown model.
[[nodiscard]] std::unique_ptr<DroneBase> make_drone(const ConnectionSettings& settings);

} // namespace drone
