# DJI Tello CLI — TODO

Current sprint. Completed tasks are removed from here at the next version bump (CHANGELOG keeps the history).
Backlog: `TODO-next-sprint.md`. Rules: `AGENTS.md`.

---

## ⚡ Next Task ID: TEL-00028

> Единственный счётчик задач. Берём это число как номер новой задачи и сразу инкрементируем.
> Номер никогда не используется повторно.

---

## 📋 Доска задач
### 🏃 Текущий спринт

| ID | Задача | Статус | Сл. |
|---|---|---|---|
| TEL-00001 | build: каркас C++20 на CMake, Rust в legacy/rust | ✅ Выполнена | 3 |
| TEL-00002 | core: UDP-транспорт и слой команд SDK | ✅ Выполнена | 5 |
| TEL-00003 | core: парсер телеметрии и StateListener | ✅ Выполнена | 3 |
| TEL-00004 | sim: фейковый дрон tello-sim | ✅ Выполнена | 5 |
| TEL-00005 | cli: REPL на C++ (паритет с Rust) | 📂 Открыта | 8 |
| TEL-00006 | core: keepalive и автопосадка при низком заряде | 📂 Открыта | 3 |
| TEL-00007 | cli: команды безопасности emergency / stop | 📂 Открыта | 2 |
| TEL-00008 | cli: миссии из файла | 📂 Открыта | 3 |
| TEL-00009 | build: make bump-dry / bump-version + CHANGELOG.md | ✅ Выполнена | 5 |
| TEL-00010 | build: make sanity-check (clang-tidy, cppcheck) | 📂 Открыта | 3 |
| TEL-00011 | build: удалить legacy/rust, README под C++ | 📂 Открыта | 2 |
| TEL-00012 | sim: режимы сбоев (потери пакетов, error, таймауты) | 📂 Открыта | 3 |
| TEL-00013 | Купить батарею и первый реальный полёт по чек-листу | ❗ Владелец | 2 |

## 📝 Детали задач

### 🔧 Build & Infrastructure

### [TEL-00001] build: каркас C++20 на CMake, Rust в legacy/rust
- **Status:** ✅ Выполнена
- **Complexity:** 3
- **Description:** Rust-прототип переносится в `legacy/rust/` (как справочник). В корне — `CMakeLists.txt`
  (C++20, `-Wall -Wextra -Wpedantic -Werror`), статическая библиотека `tello`, GoogleTest
  (системный `/usr/src/googletest`, иначе FetchContent v1.14.0), `ctest`. `Makefile` — обёртка:
  `build`, `test`, `sim`, `run-sim`, `clean`. Версия читается из `Makefile.version`.

### [TEL-00009] build: make bump-dry / bump-version + CHANGELOG.md
- **Status:** ✅ Выполнена
- **Complexity:** 5
- **Description:** Порт `panorama-ai/scripts/bump.py`: patch-бамп `Makefile.version`, секция CHANGELOG
  из `git log` с прошлого бампа (`[TEL-…] BF` → Fixed, остальное → Added, `docs/chore` пропускать),
  штамп `✅ Выполнена (vX.Y.Z, date)` и удаление выполненных задач из TODO.md, перегенерация доски,
  коммит `chore: bump version to vX.Y.Z`. `bump-dry` ничего не пишет.

### [TEL-00010] build: make sanity-check (clang-tidy, cppcheck)
- **Status:** Открыта
- **Complexity:** 3
- **Description:** `make sanity-check` → `clang-tidy` (bugprone, cppcoreguidelines, modernize, performance)
  по `compile_commands.json` + `cppcheck --enable=warning,performance`. Вывод в `logs/sanity/last.txt`.

### [TEL-00011] build: удалить legacy/rust, README под C++
- **Status:** Открыта
- **Complexity:** 2
- **Description:** После TEL-00005 (паритет CLI): удалить `legacy/rust/`, переписать `README.md`
  (сборка, `make sim`, команды), убрать Rust-строки из `.gitignore`.
- **Блокирована:** TEL-00005

### 🛰️ Core

### [TEL-00002] core: UDP-транспорт и слой команд SDK
- **Status:** ✅ Выполнена
- **Complexity:** 5
- **Description:** `ITransport` (send → reply с таймаутом) — единственный шов для моков.
  `UdpTransport` на POSIX-сокете (RAII). `Drone`: `connect` (`command`), `takeoff`, `land`, движения
  20..500 см, `cw/ccw` 1..360, `flip`, `speed` 10..100, `go`, `rc`, `stop`, `emergency`, чтения
  `battery?`/`height?`/`temp?`/`wifi?`/`sdk?`/`sn?`/`time?`. Валидация диапазонов до отправки,
  `error`/таймаут → `TelloError`. Юнит-тесты на `MockTransport`.

### [TEL-00003] core: парсер телеметрии и StateListener
- **Status:** ✅ Выполнена
- **Complexity:** 3
- **Description:** `parse_state("pitch:0;roll:0;...;\r\n")` → `DroneState` (pitch, roll, yaw, vgx/vgy/vgz,
  templ/temph, tof, h, bat, baro, time, agx/agy/agz). `StateListener` — фоновый поток, UDP 8890,
  хранит последнее состояние и время его получения. Тесты парсера + приём с localhost.

### [TEL-00006] core: keepalive и автопосадка при низком заряде
- **Status:** Открыта
- **Complexity:** 3
- **Description:** Дрон сам садится через 15 с без команд. Фоновый keepalive (`command` раз в ~5 с,
  если не было других команд). По телеметрии `bat` ≤ порога (по умолчанию 15 %) — предупреждение,
  ≤ 10 % — `land`. Проверка на `tello-sim` (его разряд батареи ускоряемый).
- **Блокирована:** TEL-00003, TEL-00004

### 💻 CLI

### [TEL-00005] cli: REPL на C++ (паритет с Rust)
- **Status:** Открыта
- **Complexity:** 8
- **Description:** Перенос `legacy/rust/src/command_line.rs`: реестр команд с категориями, `help`,
  `version`, `info`, `wait`, `exit`, несколько команд через `;`, история и автодополнение
  (`replxx` через FetchContent), задержки по командам заменить ожиданием ответа `ok`.
  Флаги `--host`, `--port`, `--state-port` — для запуска против симулятора. Без несуществующих
  медиа-команд (см. AGENTS.md → SDK facts). Позиционные команды (`camera_to_center` и т.п.) — TEL-00021.

### [TEL-00007] cli: команды безопасности emergency / stop
- **Status:** Открыта
- **Complexity:** 2
- **Description:** `emergency` (выключить моторы, без подтверждения — это аварийная кнопка),
  `hover` → `stop`. Горячая клавиша Ctrl+C в полёте → `land`, не выход из программы.

### [TEL-00008] cli: миссии из файла
- **Status:** Открыта
- **Complexity:** 3
- **Description:** `mission <file>` и `tello --mission <file>`: команды CLI построчно, `#` — комментарии,
  остановка и `land` при первой ошибке. `square <size>` как встроенный пример. Тест на симуляторе.

### 🧪 Simulator

### [TEL-00004] sim: фейковый дрон tello-sim
- **Status:** ✅ Выполнена
- **Complexity:** 5
- **Description:** Пока нет батареи — всё проверяем на нём. Библиотека + исполняемый `tello-sim`:
  UDP-сервер команд на `127.0.0.1` (порт настраивается, 0 = эфемерный), ответы как у дрона
  (`ok`, `error Not joystick`, `error Motor stop` и т.п. по диапазонам SDK), модель: SDK-режим,
  в воздухе/на земле, высота, позиция и курс, батарея с ускоряемым разрядом, автопосадка после 15 с
  тишины. Телеметрия ~10 Гц на state-порт клиента. Интеграционные тесты `UdpTransport` + `Drone`
  против симулятора в том же процессе.

### [TEL-00012] sim: режимы сбоев (потери пакетов, error, таймауты)
- **Status:** Открыта
- **Complexity:** 3
- **Description:** Флаги `tello-sim`: `--drop N%` (потеря ответов), `--delay MS`, `--fail-cmd <name>`
  (всегда `error`), `--battery N`. Тесты: ретраи и таймауты транспорта, поведение CLI при ошибке.
- **Блокирована:** TEL-00004

### 🚁 Owner

### [TEL-00013] Купить батарею и первый реальный полёт по чек-листу
- **Status:** ❗ Требует действий владельца
- **Complexity:** 2
- **Description:** Батарея заказывается. Первый полёт: открытое место без ветра, `battery?`, `takeoff`,
  `up 50`, `cw 90`, `land`; затем keepalive (постоять 20 с в воздухе) и `emergency` на высоте < 30 см.
  Результаты — новыми задачами в Sandbox.

---

## 📝 User Sandbox (Писать сюда свои задачи)
