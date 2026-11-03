#pragma once

#include "drone/errors.hpp"

namespace tello {

// Tello failure: timeout, "error ..." reply, invalid argument, socket error.
class TelloError : public drone::DroneError {
public:
    using drone::DroneError::DroneError;
};

}  // namespace tello
