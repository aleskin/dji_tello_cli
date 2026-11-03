# DJI Tello CLI — Backlog (next sprint)

Ideas and deferred tasks. The owner moves tasks from here into `TODO.md` at sprint start.
Task numbers come from the `Next Task ID` counter in `TODO.md`.
Source: the old `ROADMAP.md` of the Rust prototype (items that relied on non-existent SDK
commands — drone-side photo storage, `exposure`, `jpeg` — were dropped or rewritten).

## 📝 Задачи

### [TEL-00018] cli: геймпад / джойстик
- **Status:** Идея
- **Complexity:** 5
- **Description:** Режим `joystick` через SDL2 GameController, мёртвая зона, кнопки takeoff/land/emergency.
- **Блокирована:** TEL-00017

### [TEL-00021] cli: позиционные фигуры (camera_to_center, круг)
- **Status:** Идея
- **Complexity:** 5
- **Description:** Перенос `camera_to_center` / `camera_from_center` из Rust; облёт точки по кругу
  через `curve`/`go`, радиус по высоте, вращение во время облёта.
- **Блокирована:** TEL-00020

### [TEL-00022] build: пакеты .deb и .rpm
- **Status:** Идея
- **Complexity:** 5
- **Description:** CPack: `make package` → `.deb`/`.rpm` с `tello` и `tello-sim`.

### [TEL-00023] core: Tello EDU — mission pads и режим точки доступа
- **Status:** Идея
- **Complexity:** 8
- **Description:** `mon`/`moff`/`mdirection`, `go x y z speed mid`, `jump`; `ap ssid pass` для
  подключения дрона к домашней сети. Только для EDU — определять по `sdk?`.

### [TEL-00025] Рой: несколько дронов (Tello EDU)
- **Status:** Идея
- **Complexity:** 21
- **Description:** Несколько EDU в одной сети, адресация по IP, синхронная хореография из файла миссии.

### [TEL-00026] Компьютерное зрение: трекинг объекта и follow-me
- **Status:** Идея
- **Complexity:** 34
- **Description:** OpenCV по кадрам из TEL-00014, удержание объекта в центре через `rc`.

### [TEL-00027] Панорама: склейка фото
- **Status:** Идея
- **Complexity:** 13
- **Description:** Серия `cw 30` + кадр, склейка OpenCV Stitcher.
