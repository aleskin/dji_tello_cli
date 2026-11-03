#include "tello/state.hpp"

#include <array>
#include <charconv>
#include <cstdio>
#include <string>

namespace tello
{
namespace
{

template <typename T> bool parse_number(std::string_view text, T& out)
{
    // from_chars takes an explicit [first, last) range: no null terminator is needed.
    const char* const first = text.data();
    const char* const end = first + text.size();
    const auto [ptr, ec] = std::from_chars(first, end, out);
    return ec == std::errc() && ptr == end;
}

// Returns false on a malformed value of a known key; sets `known` when the key is ours.
bool assign(DroneState& s, std::string_view key, std::string_view value, bool& known)
{
    struct IntField
    {
        std::string_view name;
        int DroneState::* member;
    };
    struct RealField
    {
        std::string_view name;
        double DroneState::* member;
    };
    static constexpr std::array<IntField, 12> kInts{{
        {"pitch", &DroneState::pitch},
        {"roll", &DroneState::roll},
        {"yaw", &DroneState::yaw},
        {"vgx", &DroneState::vgx},
        {"vgy", &DroneState::vgy},
        {"vgz", &DroneState::vgz},
        {"templ", &DroneState::templ},
        {"temph", &DroneState::temph},
        {"tof", &DroneState::tof},
        {"h", &DroneState::h},
        {"bat", &DroneState::bat},
        {"time", &DroneState::time},
    }};
    static constexpr std::array<RealField, 4> kReals{{
        {"baro", &DroneState::baro},
        {"agx", &DroneState::agx},
        {"agy", &DroneState::agy},
        {"agz", &DroneState::agz},
    }};

    for (const auto& f : kInts)
    {
        if (f.name == key)
        {
            known = true;
            return parse_number(value, s.*f.member);
        }
    }
    for (const auto& f : kReals)
    {
        if (f.name == key)
        {
            known = true;
            return parse_number(value, s.*f.member);
        }
    }
    known = false;
    return true;
}

std::string_view trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

} // namespace

std::optional<DroneState> parse_state(std::string_view text)
{
    DroneState state;
    bool any_known = false;

    while (!text.empty())
    {
        const auto semi = text.find(';');
        const std::string_view item = trim(text.substr(0, semi));
        text = semi == std::string_view::npos ? std::string_view{} : text.substr(semi + 1);

        const auto colon = item.find(':');
        if (colon == std::string_view::npos)
        {
            continue; // trailing "\r\n" or garbage between fields
        }
        bool known = false;
        if (!assign(state, item.substr(0, colon), item.substr(colon + 1), known))
        {
            return std::nullopt;
        }
        any_known = any_known || known;
    }

    if (!any_known)
    {
        return std::nullopt;
    }
    return state;
}

std::string format_state(const DroneState& s, bool mission_pad_fields)
{
    std::array<char, 512> buf{};
    std::snprintf(buf.data(), buf.size(),
                  "%spitch:%d;roll:%d;yaw:%d;vgx:%d;vgy:%d;vgz:%d;"
                  "templ:%d;temph:%d;tof:%d;h:%d;bat:%d;baro:%.2f;time:%d;agx:%.2f;agy:%.2f;"
                  "agz:%.2f;\r\n",
                  mission_pad_fields ? "mid:-1;x:0;y:0;z:0;mpry:0,0,0;" : "", s.pitch, s.roll,
                  s.yaw, s.vgx, s.vgy, s.vgz, s.templ, s.temph, s.tof, s.h, s.bat, s.baro, s.time,
                  s.agx, s.agy, s.agz);
    return buf.data();
}

} // namespace tello
