# DJI Tello CLI

Interactive command-line controller for the DJI / Ryze Tello drone, written in C++20,
with a fake drone (`tello-sim`) so everything can be tried without flying.

## Requirements

- Linux, `g++` ≥ 13 or `clang++` ≥ 16, `cmake` ≥ 3.20
- FFmpeg development libraries (`libavcodec-dev libavformat-dev libavutil-dev`, with libx264)
- GoogleTest sources in `/usr/src/googletest` (otherwise CMake downloads v1.14.0)
- Internet on the first configure: the line editor [replxx](https://github.com/AmokHuginnsson/replxx)
  is fetched by CMake

```bash
sudo apt install cmake g++ googletest libavcodec-dev libavformat-dev libavutil-dev
```

Optional, for `make sanity-check`: `sudo apt install clang-tidy cppcheck`.

## Build and test

```bash
make build      # build/tello, build/tello-sim
make test       # unit tests + integration tests against the simulator
make clean
```

## Try it without a drone

```bash
make run-sim    # starts tello-sim on 127.0.0.1:8889 and the CLI against it
```

or in two terminals:

```bash
make sim                                     # terminal 1: the fake drone, logs every command
./build/tello --host 127.0.0.1 --port 8889   # terminal 2
```

After `streamon` the simulator sends a moving H.264 test picture to UDP 11111, the same format
as the drone's camera (960×720, 30 fps); `ffplay -fflags nobuffer udp://127.0.0.1:11111` shows it.

`tello-sim --help` lists its options: drone model (`--model tello|tello-edu`), starting battery and
accelerated drain (`--battery`, `--drain`), and failure modes (`--drop`, `--delay`, `--fail-cmd`).

## Fly

1. Power on the drone and join its Wi-Fi (`TELLO-XXXXXX`).
2. Run `./build/tello` (defaults: drone `192.168.10.1:8889`, telemetry on UDP 8890).
   For a Tello EDU add `--model tello-edu`.
3. Type `help`.

```
tello> takeoff; up 50; cw 90
tello> battery
tello> land
```

Options: `--host`, `--port`, `--state-port` (0 = no telemetry), `--video-port`, `--model`,
`--mission FILE`.

## Commands

Only the commands the selected model supports are shown by `help`. Several commands go on one
line separated by `;`; the line stops at the first error.

| Group | Commands |
|---|---|
| System | `help [cmd]`, `version`, `info`, `wait <s>`, `exit` |
| Flight | `connect`, `takeoff`, `land`, `hover` (EDU), `emergency` (motors off, no confirmation) |
| Movement | `forward/back/left/right/up/down <20..500 cm>`, `cw/ccw <1..360>`, `flip <l\|r\|f\|b>`, `speed <10..100>`, `go <x> <y> <z> <speed>` |
| Mission | `mission <file>`, `square <cm>` |
| Video | `video start` (record the camera to `~/Videos/tello/tello_DATE_TIME.mp4`), `video stop`, `video status`, `photo` (JPEG of the current picture to `~/Pictures/tello/`) |
| Telemetry | `battery`, `height`, `wifi`, `get_speed`, `temp`, `time`, `attitude`, `baro`, `tof`, `sdk` (EDU), `sn` (EDU), `state` |

`takeoff` has no height argument: take off, then `up <cm>`.

### Safety

- **Keepalive**: the drone lands by itself after 15 s without commands; the CLI sends a keepalive
  when you are quiet.
- **Low battery**: a warning at 15 %, an automatic `land` at 10 % (from telemetry).
- **Ctrl+C** in flight lands the drone; it never quits the program with the drone in the air.
  On the ground it only clears the line. `exit` or Ctrl+D quits.

### Missions

A mission is a text file with one command line per line; `#` starts a comment.
On the first failing line the mission stops and the drone lands.

```
# mission.txt
takeoff
up 50
square 100
land
```

```bash
./build/tello --mission mission.txt        # exit code 0 only if every line succeeded
```

## Architecture

Three layers; the control code (CLI, missions, keepalive) knows only the first one:

1. `drone::DroneBase` (`include/drone/`): vendor-neutral commands; each defaults to
   `NotSupported`, and `capabilities()` says what a model has.
2. `tello::TelloFamily` (`include/tello/`): the Tello text protocol over UDP, SDK 1.3 command set.
3. Models: `TelloClassic` (original Tello, SDK 1.3) and `TelloEdu` (SDK 2.0: `stop`, `sdk?`, `sn?`).

`drone::make_drone()` is the only place that picks a model. See `AGENTS.md` for the conventions.

## Versioning

`Makefile.version` holds `MAJOR.MINOR.PATCH`. `make bump-dry` previews a release,
`make bump-version` writes it: version, `CHANGELOG.md`, `TODO.md`, one commit.

## License

MIT.
