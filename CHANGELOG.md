# Changelog

All notable changes to DJI Tello CLI are documented here.
Format: `## [version] — YYYY-MM-DD` followed by grouped changes. Written by `make bump-version`.

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
