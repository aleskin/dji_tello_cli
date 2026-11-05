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
find_package(PkgConfig REQUIRED)
# FFmpeg: the simulator encodes its test video, the CLI (TEL-00014) decodes the drone's stream.
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil)
# SDL2: the live camera window (TEL-00016).
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

# Strict warnings for our own targets only: third-party code (GoogleTest) is built as is.
function(tello_strict target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

# Session log (TEL-00056): every layer writes to it, the CLI opens the file.
add_library(tello_log STATIC src/applog/log.cpp)
target_include_directories(tello_log PUBLIC include)
tello_strict(tello_log)

# Drone library.
add_library(tello STATIC
    src/version.cpp
    src/udp_socket.cpp
    src/udp_transport.cpp
    src/drone_base.cpp
    src/tello_family.cpp
    src/factory.cpp
    src/flight_guard.cpp
    src/dead_reckoning.cpp
    src/state.cpp
    src/state_listener.cpp
)
target_include_directories(tello PUBLIC include)
target_link_libraries(tello PUBLIC tello_log Threads::Threads)
target_compile_definitions(tello PRIVATE TELLO_VERSION="${TELLO_VERSION}")
tello_strict(tello)

# Fake drone: a library (linked into the tests) and the tello-sim executable.
add_library(tello_sim STATIC sim/simulator.cpp sim/video_streamer.cpp)
target_include_directories(tello_sim PUBLIC sim)
target_link_libraries(tello_sim PUBLIC tello PRIVATE PkgConfig::FFMPEG)
tello_strict(tello_sim)

add_executable(tello-sim sim/main.cpp)
target_link_libraries(tello-sim PRIVATE tello_sim)
tello_strict(tello-sim)

# Video: camera stream reader and MP4 recorder, vendor-neutral.
add_library(tello_video STATIC src/video/video_stream.cpp src/video/viewer.cpp)
target_include_directories(tello_video PUBLIC include)
target_link_libraries(tello_video PUBLIC tello_log Threads::Threads PRIVATE PkgConfig::FFMPEG)
tello_strict(tello_video)

# Live picture window (SDL2), plugged into the CLI binary; the command layer stays SDL-free.
add_library(tello_window STATIC src/video/video_window.cpp)
target_link_libraries(tello_window PUBLIC tello_video PRIVATE PkgConfig::SDL2)
tello_strict(tello_window)

# CLI: command layer as a library (tested), the binary adds line editing (replxx).
add_library(tello_cli_core STATIC src/cli/command_processor.cpp src/cli/config.cpp src/cli/figure_plan.cpp
    src/cli/flight_log.cpp)
target_include_directories(tello_cli_core PUBLIC src)
target_link_libraries(tello_cli_core PUBLIC tello tello_video)
tello_strict(tello_cli_core)

include(FetchContent)
set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(REPLXX_BUILD_PACKAGE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(replxx
    URL https://github.com/AmokHuginnsson/replxx/archive/refs/tags/release-0.0.4.tar.gz)
FetchContent_MakeAvailable(replxx)

# The target name differs from the library; the binary is still called "tello".
add_executable(tello_cli src/main.cpp)
target_link_libraries(tello_cli PRIVATE tello_cli_core tello_window replxx::replxx)
set_target_properties(tello_cli PROPERTIES OUTPUT_NAME tello)
tello_strict(tello_cli)

# Packages (make package): only the "runtime" component goes in, replxx's own install rules stay out.
include(GNUInstallDirs)
install(TARGETS tello_cli tello-sim RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
install(FILES README.md CHANGELOG.md DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/tello COMPONENT runtime)

set(CPACK_PACKAGE_NAME "tello")
set(CPACK_PACKAGE_VERSION "${TELLO_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Command-line controller for the DJI Tello drone, with a simulator")
set(CPACK_PACKAGE_DESCRIPTION
    "tello: interactive CLI for the DJI Tello (SDK 1.3/2.0): flight commands, missions, live video, photos, keyboard and gamepad flying. tello-sim: a fake drone on localhost.")
set(CPACK_PACKAGE_VENDOR "Aleksandr Leskin")
set(CPACK_PACKAGE_CONTACT "Aleksandr Leskin <aleskinos@gmail.com>")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};runtime;/")
set(CPACK_STRIP_FILES ON)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # FFmpeg and SDL2 runtime libraries
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _build_id_links none")
include(CPack)

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
        tests/test_command_processor.cpp
        tests/test_flight_guard.cpp
        tests/test_failures.cpp
        tests/test_sim_video.cpp
        tests/test_video_stream.cpp
        tests/test_video_window.cpp
        tests/test_keyboard.cpp
        tests/test_joystick.cpp
        tests/test_config.cpp
        tests/test_aliases.cpp
        tests/test_position.cpp
        tests/test_figures.cpp
        tests/test_photo_figures.cpp
        tests/test_log.cpp
    )
    target_include_directories(tello_tests PRIVATE tests)
    target_link_libraries(tello_tests PRIVATE tello_sim tello_cli_core tello_window PkgConfig::FFMPEG GTest::gtest_main)
    tello_strict(tello_tests)
    include(GoogleTest)
    gtest_discover_tests(tello_tests)
endif()
