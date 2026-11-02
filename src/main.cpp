#include <iostream>

#include "tello/version.hpp"

int main() {
    // The interactive shell arrives with TEL-00005.
    std::cout << "tello " << tello::version() << '\n';
    return 0;
}
