# AGENTS.md — DJI Tello CLI Project Guide for AI Assistants

Interactive command-line controller for the DJI Tello drone, written in **C++20**.
The old Rust prototype lives in `legacy/rust/` until the C++ CLI reaches parity (TEL-00011).

---

## 🚨 CRITICAL RULES — READ BEFORE ANYTHING ELSE

1. **NEVER SEND COMMANDS TO A REAL DRONE.**
   The drone address `192.168.10.1` must never appear in tests, scripts or anything the AI runs.
   Tests and manual checks go to the simulator (`tello-sim`) on `127.0.0.1` only.
   Real flights are done by the owner only — a wrong command means a drone in a tree.

2. **VERSION BUMP — PATCH ONLY, NEVER ON YOUR OWN.**
   `Makefile.version` is the single source of truth for the version. Do not touch it by hand.
   When the owner says "бамп" / "bump" → `make bump-dry`, show output, wait for "делай", then `make bump-version`.
   `TYPE=minor` / `TYPE=major` only if the owner says exactly "bump minor" / "bump major".
   (Bump tooling: TEL-00009. Until it exists — do not bump at all.)

3. **COMMIT WHEN A TEL TASK IS COMPLETED. NEVER PUSH.**
   One task = one commit, format below. `git push` is **strictly forbidden** — the owner pushes manually.
   Do not commit unrelated uncommitted changes together with a task.

4. **DO EXACTLY WHAT WAS ASKED. NOTHING MORE.**
   No unrequested features, refactors or "improvements".

---

## 📝 COMMIT CONVENTION — STRICT

```
[TEL-XXXXX] BF <module>: <short English description>   ← bugfix
[TEL-XXXXX] <module>: <short English description>       ← feature/improvement
```

| Field | Values | Notes |
|---|---|---|
| `TEL-XXXXX` | 5-digit zero-padded task number | mandatory for code changes |
| `BF` | bugfixes only | omit for features |
| `<module>` | `core` / `cli` / `sim` / `video` / `build` | main affected area |
| `<description>` | English, imperative, ≤72 chars | no period at end |

Examples:
```
[TEL-00002] core: add UDP transport and SDK command layer
[TEL-00004] sim: add fake Tello UDP simulator
[TEL-00015] BF video: fix JPEG frame grab on keyframe boundary
docs(agents): update commit convention
chore: bump version to v0.1.1
```

- Commit messages in **English only**. No Claude/Anthropic/AI attribution lines.
- `docs(...)` / `chore(...)` commits need no TEL number and do not go to CHANGELOG.
- Subtasks: `TEL-XXXXX-YY`, listed explicitly under the parent task in `TODO.md`.

---

## 📋 TODO SYSTEM (same as panorama-ai)

| File | Purpose |
|---|---|
| `TODO.md` | current sprint: board (top) + task details (bottom), max **30** open tasks |
| `TODO-next-sprint.md` | backlog: ideas and deferred tasks |

- **Next Task ID** — the `## ⚡ Next Task ID: TEL-XXXXX` line in `TODO.md` is the only counter.
  Take the number, increment the counter in the same edit. Never reuse a number.
- Task block:
  ```
  ### [TEL-XXXXX] <module>: <title>
  - **Status:** Открыта
  - **Complexity:** 3
  - **Description:** ...
  ```
- **Status values (exact):** `Идея`, `Открыта`, `В работе`, `✅ Выполнена`, `❗ Требует действий владельца`, `Заблокирована`, `Закрыта (<reason>)`.
- Done = `- **Status:** ✅ Выполнена` and nothing more. Never delete a finished task by hand — the bump removes it.
- **Complexity** = hours on the Fibonacci scale 1, 2, 3, 5, 8, 13, 21, 34, 55 (round up).
- Over 30 open tasks in `TODO.md` → start every answer with
  `⚠️ ГОРШОЧЕК НЕ ВАРИ! Открытых задач в TODO.md: N/30.`
- New idea from the owner → write it to `TODO.md` / backlog first, implement after confirmation.
- `## 📝 User Sandbox` at the bottom of `TODO.md` is the owner's inbox: convert every item into a
  task ("вижу N идей, создаю N задач"), then clear the text, keep the header.

**Task completion checklist:** implement → `make test` green → status `✅ Выполнена` in `TODO.md` → commit.

---

## 🛠️ BUILD AND TEST

```bash
make build        # cmake configure + build (build/)
make test         # unit + simulator integration tests (ctest)
make sim          # run the fake drone on 127.0.0.1:8889
make run-sim      # run the CLI against the simulator
make clean
```

Requirements: `cmake` ≥ 3.20, `g++` ≥ 13 or `clang++` ≥ 16, `googletest` (`apt install cmake googletest`).
If `/usr/src/googletest` is missing, CMake downloads GoogleTest v1.14.0.

---

## 🧱 ARCHITECTURE

```
include/drone/        layer 1, vendor-neutral (the control layer knows only this)
  drone_base.hpp      DroneBase — every command a stub throwing NotSupported; capabilities()
  factory.hpp         make_drone(settings) — the only place that knows concrete models
include/tello/        layer 2 (family) and layer 3 (models)
  tello_family.hpp    TelloFamily — Tello protocol, SDK 1.3 set, range validation, TelloError;
                      TelloClassic (owner's drone, SDK 1.3), TelloEdu (SDK 2.0: stop, sdk?, sn?)
  transport.hpp       ITransport — send a command, wait for the reply (one seam for mocks)
  udp_transport.hpp   UdpTransport — real UDP socket (POSIX)
  state.hpp           DroneState + parse_state() for the telemetry string
  state_listener.hpp  StateListener — background UDP receiver of telemetry
src/                  library implementation + CLI main
sim/                  tello-sim: fake drone (library + executable), used by tests and by hand
tests/                GoogleTest: MockTransport unit tests + simulator integration tests
legacy/rust/          old Rust prototype (reference only, removed in TEL-00011)
```

Rules:
- **C++20**, warnings are errors (`-Wall -Wextra -Wpedantic -Werror`).
- RAII for every resource (sockets, threads); no raw `new`/`delete`; no `using namespace std`.
- Errors: `tello::TelloError` (exception) from the library; the CLI catches and prints.
- Every new SDK command: virtual stub in `DroneBase` + `Capability`, implementation in the family
  (all models) or the model (one firmware), unit test with `MockTransport`, reply in `tello-sim`.
- CLI, missions, keepalive use `DroneBase` only — never a concrete family or model.
- Tests bind only to `127.0.0.1` with port `0` (ephemeral) — never fixed ports, never the real drone.
- Library code does not print; output belongs to the CLI.

---

## 📡 TELLO SDK FACTS (verify here before inventing a command)

| Port | Direction | Purpose |
|---|---|---|
| 8889/udp | PC → drone `192.168.10.1` | commands, reply to the sender: `ok` / `error ...` / value |
| 8890/udp | drone → PC | telemetry string `pitch:0;roll:0;...;\r\n` ~10 Hz |
| 11111/udp | drone → PC | H.264 video after `streamon` |

- First command must be `command` (enter SDK mode).
- Drone **auto-lands after 15 s without any command** — keepalive needed (TEL-00006).
- Moves: `up/down/left/right/forward/back x` with x = 20..500 cm; `cw/ccw x` x = 1..360;
  `flip l|r|f|b`; `speed x` x = 10..100; `go x y z speed`; `rc a b c d` (−100..100, no reply);
  `stop` (hover), `emergency` (motors off).
- Reads: `battery?`, `speed?`, `time?`, `wifi?`, `sdk?`, `sn?`, `height?`, `temp?`, `attitude?`, `baro?`, `tof?`.
- **There is no photo storage, no `snapshot`/`takepic`/`ls`/`download`/`rm` and no `exposure`/`jpeg`.**
  Photo = grab a frame from the 11111 video stream on the PC (TEL-00015). The Rust prototype got this wrong.
- `takeoff` has no height argument: height = `takeoff` + `up x`.

---

## 🧪 NO DRONE? USE THE SIMULATOR

`tello-sim` emulates the drone on localhost: replies to SDK commands with the same ranges and
`error` texts, models flying/landed, height, position, battery drain, sends telemetry to the
client's state port, auto-lands after the 15 s silence. Every feature must be checkable against it.

---

## Owner's working style
- Owner chats in Russian; answers in Russian, code/comments/commits in English.
- Owner thinks out loud, ideas arrive incrementally — record them as tasks first.
- Minimum manual steps; automate through `make`.
