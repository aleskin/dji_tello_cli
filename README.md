# DJI Tello CLI

Interactive command-line controller for the DJI / Ryze Tello drone, written in C++20,
with a fake drone (`tello-sim`) so everything can be tried without flying.

## Requirements

- Linux, `g++` ≥ 13 or `clang++` ≥ 16, `cmake` ≥ 3.20
- FFmpeg development libraries (`libavcodec-dev libavformat-dev libavutil-dev`, with libx264)
- SDL2 development library (`libsdl2-dev`) for the live camera window
- GoogleTest sources in `/usr/src/googletest` (otherwise CMake downloads v1.14.0)
- Internet on the first configure: the line editor [replxx](https://github.com/AmokHuginnsson/replxx)
  is fetched by CMake

```bash
sudo apt install cmake g++ googletest libavcodec-dev libavformat-dev libavutil-dev libsdl2-dev
```

Optional, for `make sanity-check`: `sudo apt install clang-tidy cppcheck`; for `make package`:
`dpkg-dev` (`.deb`) and `rpm` (`.rpm`).

## Build and test

```bash
make build      # build/tello, build/tello-sim
make test       # unit tests + integration tests against the simulator
make package    # Release build-package/tello_<ver>_amd64.deb and tello-<ver>-1.x86_64.rpm
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
| System | `help [cmd]`, `version`, `info`, `wait <s>`, `alias`, `aliases`, `unalias <name>`, `exit` |
| Flight | `connect`, `takeoff`, `land`, `hover` (EDU), `emergency` (motors off, no confirmation) |
| Movement | `forward/back/left/right/up/down <20..500 cm>`, `cw/ccw <1..360>`, `flip <l\|r\|f\|b>`, `speed <10..100>`, `go <x> <y> <z> <speed>`, `return` / `home` (fly back above the takeoff point, then `land`) |
| Figures | `camera_to_center <x> <y>` / `camera_from_center <x> <y>` (turn the camera towards / away from a point, cm as `position` shows), `orbit <50..500 cm> [cw\|ccw] [speed]` / `circle` (circle the point that far ahead in 12 legs of 30°, camera to the center) |
| Mission | `mission <file>`, `square <cm>` |
| Photo figures | `flower [height] [radius] [step] [in\|out] [cw\|ccw] [speed]` (take off, horizontal circle around the takeoff point, a photo every `step` degrees, camera `in` to the center or `out`), `wheel [height] [radius] [step] [cw\|ccw] [in\|out] [speed]` (vertical circle across your view, from the top; `cw` goes right first), `loop [height] [radius] [step] [away\|toward] [in\|out\|side] [speed]` (vertical circle away from you and back), `panorama [height] [step] [cw\|ccw]` (turn on the spot with a photo every `step` degrees), `tower [from] [to] [step]` (climb straight up with a photo every `step` cm, 100..500 by 50), `dronie [distance] [height] [speed]` (video: camera on you, away 300 cm and up 200 cm, back), `reveal [distance] [height] [speed]` (video: from 50 cm forward and up, the view opens up), `bounce [times] [amplitude] [cw\|ccw] [speed]` (up and down, with `cw`/`ccw` one full turn on the way), `flips [l\|r\|f\|b ...]` (flips at 150 cm, all four by default, needs 50 % battery), `helix [turns] [from height] [to height] [from radius] [to radius] [step] [cw\|ccw] [speed]` (video: spiral up and out around the takeoff point, camera in; 2 turns, 100..300 cm, radius 100..200 cm), `boomerang [length] [width] [rise] [cw\|ccw] [speed]` (video: an oval around the takeoff point, rising at the far end, camera in), `eight [radius] [height] [step] [flat\|upright] [speed]` (video: a figure eight, flat or upright), `polygon <corners> [radius] [height] [flat\|upright] [speed]` and `star [radius] [height] [flat\|upright] [speed]` (draw the shape in the air), `rings [radius] [from height] [to height] [rings] [step] [speed]` (photo rings at several heights, camera in, one folder for a 3D model), `timelapse [interval] [frames] [height]` and `hyperlapse [distance] [frames] [height] [speed]` (frames hovering or flying slowly forward, put into `<figure>.mp4` at 10 fps next to them), `scan [width] [from height] [to height] [step] [speed]` (facing a wall: rows of photos every `step` cm in a snake, 300 cm wide, 100..300 cm). Defaults: 200 cm, 100 cm, 30°, `in`, `cw`/`away`, 30 cm/s. Each figure ends above the takeoff point and lands; photos go to `~/Pictures/tello/<figure>_DATE_TIME/01.jpg...`. Add `dry` to any figure to see its commands, photos, time, height and battery estimate without sending anything; add `video` to record the whole flight to `~/Videos/tello` (a recording you started yourself just goes on) |
| Keyboard | `keyboard` / `kb` (in flight: fly with the keys in the video window — WASD move, arrows up/down/turn, Esc back to commands), `kbspeed [10..100]` (stick deflection, default 40) |
| Joystick | `joystick` / `js` (gamepad through the video window, works on the ground too: right stick moves, left stick climbs/turns, `a` takeoff, `b` land, `back`+`start` together emergency, Esc in the window or `guide` back) |
| Video | `video start` (record the camera to `~/Videos/tello/tello_DATE_TIME.mp4`), `video stop`, `video show` / `video hide` (live window with battery, height and heading), `video status`, `photo` (JPEG of the current picture to `~/Pictures/tello/`) |
| Telemetry | `battery`, `height`, `wifi`, `get_speed`, `temp`, `time`, `attitude`, `baro`, `tof`, `sdk` (EDU), `sn` (EDU), `state`, `position` / `pos` (x y z from the takeoff point, heading, estimated error) |

`takeoff` has no height argument: take off, then `up <cm>`.

The Tello camera looks straight ahead and cannot tilt: the takeoff point is in the picture only when the radius is more than about twice the height. `loop` passes `radius` cm behind the takeoff point: stand aside or further back.

The Tello has no satellites: its position is **dead reckoning** — counted from the completed
commands and, in keyboard mode, from the telemetry speed. The error estimate grows with every
metre flown; `return` flies straight legs back by this estimate, it is not a GPS return-to-home.

### Safety

- **Keepalive**: the drone lands by itself after 15 s without commands; the CLI sends a keepalive
  when you are quiet.
- **Low battery**: a warning at 15 %, an automatic `land` at 10 % (from telemetry; see the config).
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

### Aliases

`alias name commands` defines a new command; `$1`..`$9` are its arguments, `$*` all of them.
The body is the rest of the line, `;` included. Aliases are saved to `~/.config/tello/aliases`
and work in missions too.

```
tello> alias hop up $1; down $1
tello> takeoff; hop 50; land
tello> aliases            # list;  alias hop = show one;  unalias hop = delete
```

### Config file

`~/.config/tello/config` (or `$XDG_CONFIG_HOME/tello/config`) sets defaults; command-line flags
override it. `key = value`, `#` starts a comment; unknown keys and bad values are reported and skipped.

```
host = 192.168.10.1
port = 8889
state_port = 8890        # 0 = telemetry off
video_port = 11111
model = tello            # or tello-edu
warn_battery = 15        # percent
land_battery = 10
kbspeed = 40             # keyboard mode, 10..100
js_deadzone = 10         # joystick mode, percent of stick travel ignored, 0..50
key_forward = w          # also key_back/left/right/up/down/turn_left/turn_right (SDL key names)
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
