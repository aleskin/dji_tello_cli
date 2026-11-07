# Changelog

All notable changes to DJI Tello CLI are documented here.
Format: `## [version] — YYYY-MM-DD` followed by grouped changes. Written by `make bump-version`.

---

## [0.1.2] — 2026-11-07

### Fixed
- **[TEL-00063]** cli: confirm land by telemetry, a late ok is not a landing
- **[TEL-00062]** video: record outside the picture lock, hover on stale window input
- **[TEL-00061]** cli: land and exit when the console output pipe closes
- **[TEL-00060]** core: short keepalive wait, send emergency ahead of the queue
- **[TEL-00059]** core: retry a failed low-battery landing until confirmed
- **[TEL-00058]** cli: treat the drone as airborne when takeoff gets no reply

---

## [0.1.1] — 2026-11-05

### Added
- **[TEL-00057]** cli: land the drone when the program fails
- **[TEL-00056]** cli: add session log for post-flight analysis

---

## [0.1.0] — 2026-11-05

### Added
- **[TEL-00055]** cli: record the flight into a mission file
- **[TEL-00054]** cli: add scan figure for walls
- **[TEL-00053]** cli: add timelapse and hyperlapse with MP4 assembly
- **[TEL-00052]** cli: add rings photo figure for 3D scans
- **[TEL-00051]** cli: add polygon and star figures
- **[TEL-00050]** cli: add figure eight video figure
- **[TEL-00049]** cli: add boomerang video figure
- **[TEL-00048]** cli: add helix video figure
- **[TEL-00047]** cli: add flips show figure
- **[TEL-00046]** cli: add bounce show figure
- **[TEL-00045]** cli: add reveal video figure
- **[TEL-00044]** cli: add dronie video figure
- **[TEL-00043]** cli: add tower photo figure
- **[TEL-00042]** cli: add panorama photo figure
- **[TEL-00041]** cli: record figure flights to video with the video flag
- **[TEL-00040]** cli: plan figures before flying and add dry run

---

## [0.0.6] — 2026-11-04

### Added
- **[TEL-00037]** **[TEL-00038]** **[TEL-00039]** cli: add flower, wheel and loop photo figures

---

## [0.0.5] — 2026-11-03

### Added
- **[TEL-00036]** build: use personal maintainer email in packages
- **[TEL-00022]** build: add make package producing .deb and .rpm via CPack
- **[TEL-00021]** cli: add camera_to_center/from_center and orbit figures
- **[TEL-00018]** cli: add joystick mode flying with a gamepad via the window

---

## [0.0.4] — 2026-11-03

### Fixed
- **[TEL-00032]** cli: keep simulator alive on Ctrl+C under make run-sim

### Added
- **[TEL-00020]** core: add position sources, dead reckoning and return home
- **[TEL-00019]** cli: add aliases with parameters and config file
- **[TEL-00017]** cli: add keyboard mode flying with rc from the video window
- **[TEL-00016]** video: add SDL2 live camera window with OSD via video show
- **[TEL-00015]** video: save the current camera picture as JPEG with photo
- **[TEL-00014]** video: receive H.264 stream and record MP4 via video command
- **[TEL-00024]** sim: stream H.264 test picture to UDP 11111 after streamon
- **[TEL-00031]** build: clear clang-tidy findings, silence style-only checks

---

## [0.0.3] — 2026-11-03

### Fixed
- **[TEL-00001]** build: reconfigure when Makefile.version changes

### Added
- **[TEL-00030]** build: apply Allman style with 4-space indent via clang-format
- **[TEL-00011]** build: remove Rust prototype and rewrite README for C++
- **[TEL-00010]** build: add make sanity-check with clang-tidy and cppcheck
- **[TEL-00012]** sim: add reply loss, delay and forced errors; retry queries
- **[TEL-00008]** cli: add missions from file and square example
- **[TEL-00007]** cli: add emergency and hover, land on Ctrl+C in flight
- **[TEL-00006]** core: add FlightGuard keepalive and low-battery landing
- **[TEL-00005]** cli: add interactive REPL over DroneBase with replxx
- **[TEL-00029]** sim: emulate classic Tello and Tello EDU models
- **[TEL-00028]** core: add drone hierarchy base, Tello family and models

---

## [0.0.2] — 2026-11-02

### Added
- **[TEL-00009]** build: add make bump-dry / bump-version and CHANGELOG
- **[TEL-00004]** sim: add fake Tello UDP simulator
- **[TEL-00003]** core: add telemetry parser and background StateListener
- **[TEL-00002]** core: add UDP transport and SDK command layer
- **[TEL-00001]** build: add C++20 CMake skeleton and move Rust to legacy/rust

---
