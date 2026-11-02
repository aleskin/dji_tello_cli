# DJI Tello CLI — Backlog (next sprint)

Ideas and deferred tasks. The owner moves tasks from here into `TODO.md` at sprint start.
Task numbers come from the `Next Task ID` counter in `TODO.md`.
Source: the old `ROADMAP.md` of the Rust prototype (items that relied on non-existent SDK
commands — drone-side photo storage, `exposure`, `jpeg` — were dropped or rewritten).

## 📝 Задачи

### [TEL-00014] video: приём H.264 с UDP 11111 и запись в файл
- **Status:** Идея
- **Complexity:** 13
- **Description:** `streamon` → приём потока на 11111, сборка NAL-юнитов, декодирование FFmpeg
  (`libavcodec`), запись в `.mp4`/`.h264` в папку `~/Videos/tello/`. `video start|stop` в CLI.

### [TEL-00015] video: фото = кадр из видеопотока в JPEG
- **Status:** Идея
- **Complexity:** 3
- **Description:** `photo` берёт последний декодированный кадр и сохраняет JPEG. Замена фейковых
  `snapshot`/`takepic` из Rust-прототипа.
- **Блокирована:** TEL-00014

### [TEL-00016] video: окно живой картинки (SDL2)
- **Status:** Идея
- **Complexity:** 8
- **Description:** Маленькое окно с потоком камеры, изменяемый размер, OSD: батарея, высота, курс.
- **Блокирована:** TEL-00014

### [TEL-00017] cli: прямое управление с клавиатуры (rc)
- **Status:** Идея
- **Complexity:** 8
- **Description:** Режим `keyboard`: WASD — вперёд/влево/назад/вправо, стрелки — вверх/вниз/поворот,
  через `rc a b c d` ~20 Гц; Esc — назад в командный режим; индикатор режима. SDL2 для клавиш.

### [TEL-00018] cli: геймпад / джойстик
- **Status:** Идея
- **Complexity:** 5
- **Description:** Режим `joystick` через SDL2 GameController, мёртвая зона, кнопки takeoff/land/emergency.
- **Блокирована:** TEL-00017

### [TEL-00019] cli: алиасы с параметрами и конфиг-файл
- **Status:** Идея
- **Complexity:** 5
- **Description:** `alias name cmds` / `aliases` / `unalias`, параметры `$1 $2`, хранение в
  `~/.config/tello/aliases`. Настройки (host, порты, пороги батареи) — `~/.config/tello/config`.

### [TEL-00020] core: счисление позиции и return-to-home
- **Status:** Идея
- **Complexity:** 5
- **Description:** Позиция и курс по выполненным командам (+ `vgx/vgy` из телеметрии), команда
  `return` — обратный путь к точке взлёта, `get_position`.

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

### [TEL-00024] sim: эмуляция видеопотока
- **Status:** Идея
- **Complexity:** 5
- **Description:** `tello-sim --video` шлёт на 11111 тестовую H.264-картинку (FFmpeg testsrc),
  чтобы проверять TEL-00014..16 без дрона.

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
