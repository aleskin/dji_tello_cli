#include "drone/factory.hpp"

#include "drone/errors.hpp"
#include "tello/tello_family.hpp"
#include "tello/udp_transport.hpp"

namespace drone
{

std::vector<std::string_view> known_models()
{
    return {"tello", "tello-edu"};
}

std::unique_ptr<DroneBase> make_drone(const ConnectionSettings& settings)
{
    const bool classic = settings.model == "tello";
    if (!classic && settings.model != "tello-edu")
    {
        throw DroneError("unknown drone model '" + settings.model + "' (known: tello, tello-edu)");
    }
    auto transport = std::make_shared<tello::UdpTransport>(
        settings.host.empty() ? std::string(tello::kDroneHost) : settings.host,
        settings.port == 0 ? tello::kCommandPort : settings.port);
    const tello::DroneTimeouts timeouts{settings.action_timeout, settings.query_timeout};
    std::unique_ptr<tello::TelloFamily> drone;
    if (classic)
    {
        drone = std::make_unique<tello::TelloClassic>(std::move(transport), timeouts);
    }
    else
    {
        drone = std::make_unique<tello::TelloEdu>(std::move(transport), timeouts);
    }
    if (settings.video_port != 0)
    {
        drone->set_video_port(settings.video_port);
    }
    return drone;
}

} // namespace drone
