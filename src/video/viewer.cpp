#include "video/viewer.hpp"

#include <array>
#include <cstdio>

namespace video
{

std::string osd_text(const Osd& osd)
{
    auto value = [](const std::optional<int>& v, const char* format)
    {
        if (!v)
        {
            return std::string("--");
        }
        std::array<char, 16> buf{};
        std::snprintf(buf.data(), buf.size(), format, *v);
        return std::string(buf.data());
    };
    return "BAT " + value(osd.battery, "%d") + "%  H " + value(osd.height_cm, "%d") + "CM  HDG " +
           value(osd.heading_deg, "%03d");
}

int heading_from_yaw(int yaw)
{
    return ((yaw % 360) + 360) % 360;
}

} // namespace video
