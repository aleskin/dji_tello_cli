#pragma once

#include <stdexcept>
#include <string>

namespace tello {

// Any failure talking to the drone: timeout, "error ..." reply, invalid argument, socket error.
class TelloError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace tello
