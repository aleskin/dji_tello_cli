# DJI Tello CLI — TODO

Current sprint. Completed tasks are removed from here at the next version bump (CHANGELOG keeps the history).
Backlog: `TODO-next-sprint.md`. Rules: `AGENTS.md`.

---

## ⚡ Next Task ID: TEL-00033

> Единственный счётчик задач. Берём это число как номер новой задачи и сразу инкрементируем.
> Номер никогда не используется повторно.

---

## 📋 Доска задач
### 🏃 Текущий спринт

| ID | Задача | Статус | Сл. |
|---|---|---|---|
| TEL-00013 | Купить батарею и первый реальный полёт по чек-листу | ❗ Владелец | 2 |
| TEL-00014 | video: приём H.264 с UDP 11111 и запись в файл | 📂 Открыта | 13 |
| TEL-00015 | video: фото = кадр из видеопотока в JPEG | 📂 Открыта | 3 |
| TEL-00016 | video: окно живой картинки (SDL2) | 📂 Открыта | 8 |
| TEL-00017 | cli: прямое управление с клавиатуры (rc) | 📂 Открыта | 8 |
| TEL-00019 | cli: алиасы с параметрами и конфиг-файл | 📂 Открыта | 5 |
| TEL-00020 | core: счисление позиции и return-to-home | 📂 Открыта | 5 |
| TEL-00024 | sim: эмуляция видеопотока | 📂 Открыта | 5 |
| TEL-00031 | build: разобрать замечания make sanity-check | 📂 Открыта | 3 |
| TEL-00032 | cli: Ctrl+C под make run-sim не должен ронять симулятор | 📂 Открыта | 2 |

## 📝 Детали задач

### 🔧 Build & Infrastructure

### [TEL-00031] build: разобрать замечания make sanity-check
- **Status:** Открыта
- **Complexity:** 3
- **Description:** 132 замечания clang-tidy (v0.0.3). Исправить реальные: `bugprone-*`
  (empty-catch с комментарием, throwing-static-init), `performance-*`, `scoped_lock`, `std::array`
  вместо C-массивов. Стилевой шум (designated initializers, unchecked `operator[]` на проверенных
  индексах) — отключить в `.clang-tidy` с причиной. Цель: отчёт без замечаний.

### 🛰️ Core

### [TEL-00020] core: счисление позиции и return-to-home
- **Status:** Открыта
- **Complexity:** 5
- **Description:** Позиция и курс по выполненным командам (+ `vgx/vgy` из телеметрии), команда
  `return` — обратный путь к точке взлёта, `get_position`.

### 💻 CLI

### [TEL-00017] cli: прямое управление с клавиатуры (rc)
- **Status:** Открыта
- **Complexity:** 8
- **Description:** Режим `keyboard`: WASD — вперёд/влево/назад/вправо, стрелки — вверх/вниз/поворот,
  через `rc a b c d` ~20 Гц; Esc — назад в командный режим; индикатор режима. SDL2 для клавиш.

### [TEL-00019] cli: алиасы с параметрами и конфиг-файл
- **Status:** Открыта
- **Complexity:** 5
- **Description:** `alias name cmds` / `aliases` / `unalias`, параметры `$1 $2`, хранение в
  `~/.config/tello/aliases`. Настройки (host, порты, пороги батареи) — `~/.config/tello/config`.

### [TEL-00032] cli: Ctrl+C под make run-sim не должен ронять симулятор
- **Status:** Открыта
- **Complexity:** 2
- **Description:** Ctrl+C в терминале получает вся группа процессов: `make`, `tello-sim`, `tello`.
  Нужно: CLI сажает дрон и продолжает работать, симулятор жив, `make` не обрывает сессию.
  Запуск симулятора в `run-sim` с игнорируемым SIGINT и проверка под `script`/pty.

### 🧪 Simulator

### [TEL-00024] sim: эмуляция видеопотока
- **Status:** Открыта
- **Complexity:** 5
- **Description:** `tello-sim --video` шлёт на 11111 тестовую H.264-картинку (FFmpeg testsrc),
  чтобы проверять TEL-00014..16 без дрона.

### 🎥 Video

### [TEL-00014] video: приём H.264 с UDP 11111 и запись в файл
- **Status:** Открыта
- **Complexity:** 13
- **Description:** `streamon` → приём потока на 11111, сборка NAL-юнитов, декодирование FFmpeg
  (`libavcodec`), запись в `.mp4`/`.h264` в папку `~/Videos/tello/`. `video start|stop` в CLI.

### [TEL-00015] video: фото = кадр из видеопотока в JPEG
- **Status:** Открыта
- **Complexity:** 3
- **Description:** `photo` берёт последний декодированный кадр и сохраняет JPEG. Замена фейковых
  `snapshot`/`takepic` из Rust-прототипа.
- **Блокирована:** TEL-00014

### [TEL-00016] video: окно живой картинки (SDL2)
- **Status:** Открыта
- **Complexity:** 8
- **Description:** Маленькое окно с потоком камеры, изменяемый размер, OSD: батарея, высота, курс.
- **Блокирована:** TEL-00014

### 🚁 Owner

### [TEL-00013] Купить батарею и первый реальный полёт по чек-листу
- **Status:** ❗ Требует действий владельца
- **Complexity:** 2
- **Description:** Батарея заказывается. Первый полёт: открытое место без ветра, `battery?`, `takeoff`,
  `up 50`, `cw 90`, `land`; затем keepalive (постоять 20 с в воздухе) и `emergency` на высоте < 30 см.
  Перед взлётом `tello --model tello-edu`, `sdk` и `sn`: ответ `unknown command` = прошивка SDK 1.3
  (наш `--model tello`), число = SDK 2.0. Результаты — новыми задачами в Sandbox.

---

## 📝 User Sandbox (Писать сюда свои задачи)
