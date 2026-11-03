cmake_minimum_required(VERSION 3.20)

# Makefile.version is the single source of truth for the version.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Makefile.version" TELLO_VERSION)
# Re-run configure when the bump rewrites the file, or binaries keep the old version.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS Makefile.version)
string(STRIP "${TELLO_VERSION}" TELLO_VERSION)
if(NOT TELLO_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "Makefile.version must be MAJOR.MINOR.PATCH, got '${TELLO_VERSION}'")
endif()

project(dji_tello_cli VERSION ${TELLO_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)

# Strict warnings for our own targets only: third-party code (GoogleTest) is built as is.
function(tello_strict target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

# Drone library.
add_library(tello STATIC
  src/version.cpp
  src/udp_socket.cpp
  src/udp_transport.cpp
  src/drone_base.cpp
  src/tello_family.cpp
  src/factory.cpp
  src/state.cpp
  src/state_listener.cpp
)
target_include_directories(tello PUBLIC include)
target_link_libraries(tello PUBLIC Threads::Threads)
target_compile_definitions(tello PRIVATE TELLO_VERSION="${TELLO_VERSION}")
tello_strict(tello)

# Fake drone: a library (linked into the tests) and the tello-sim executable.
add_library(tello_sim STATIC sim/simulator.cpp)
target_include_directories(tello_sim PUBLIC sim)
target_link_libraries(tello_sim PUBLIC tello)
tello_strict(tello_sim)

add_executable(tello-sim sim/main.cpp)
target_link_libraries(tello-sim PRIVATE tello_sim)
tello_strict(tello-sim)

# CLI. The target name differs from the library; the binary is still called "tello".
add_executable(tello_cli src/main.cpp)
target_link_libraries(tello_cli PRIVATE tello)
set_target_properties(tello_cli PROPERTIES OUTPUT_NAME tello)
tello_strict(tello_cli)

option(TELLO_BUILD_TESTS "Build unit and simulator tests" ON)
if(TELLO_BUILD_TESTS)
  enable_testing()
  if(EXISTS /usr/src/googletest/CMakeLists.txt)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(/usr/src/googletest ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(tello_tests
    tests/test_version.cpp
    tests/test_tello_family.cpp
    tests/test_drone_base.cpp
    tests/test_udp_transport.cpp
    tests/test_state.cpp
    tests/test_simulator.cpp
  )
  target_include_directories(tello_tests PRIVATE tests)
  target_link_libraries(tello_tests PRIVATE tello_sim GTest::gtest_main)
  tello_strict(tello_tests)
  include(GoogleTest)
  gtest_discover_tests(tello_tests)
endif()
