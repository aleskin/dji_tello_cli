#pragma once

#include <string_view>

namespace tello {

// Project version, taken from Makefile.version at configure time.
std::string_view version() noexcept;

}  // namespace tello
