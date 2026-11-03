#pragma once

#include <stdexcept>
#include <string>

namespace drone {

// Any failure of a drone command, whatever the vendor.
class DroneError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The model has no such command (base-class stub). Nothing was sent to the drone.
class NotSupported : public DroneError {
public:
    using DroneError::DroneError;
};

}  // namespace drone
