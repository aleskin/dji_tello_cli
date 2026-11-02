#include <iostream>

#include "tello/version.hpp"

int main() {
    // The simulator arrives with TEL-00004.
    std::cout << "tello-sim " << tello::version() << '\n';
    return 0;
}
