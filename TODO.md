# DJI Tello CLI — TODO

Current sprint. Completed tasks are removed from here at the next version bump (CHANGELOG keeps the history).
Backlog: `TODO-next-sprint.md`. Rules: `AGENTS.md`.

---

## ⚡ Next Task ID: TEL-00030

> Единственный счётчик задач. Берём это число как номер новой задачи и сразу инкрементируем.
> Номер никогда не используется повторно.

---

## 📋 Доска задач
### 🏃 Текущий спринт

| ID | Задача | Статус | Сл. |
|---|---|---|---|
| TEL-00005 | cli: REPL на C++ (паритет с Rust) | ✅ Выполнена | 8 |
| TEL-00006 | core: keepalive и автопосадка при низком заряде | ✅ Выполнена | 3 |
| TEL-00007 | cli: команды безопасности emergency / stop | ✅ Выполнена | 2 |
| TEL-00008 | cli: миссии из файла | ✅ Выполнена | 3 |
| TEL-00010 | build: make sanity-check (clang-tidy, cppcheck) | 📂 Открыта | 3 |
| TEL-00011 | build: удалить legacy/rust, README под C++ | 📂 Открыта | 2 |
| TEL-00012 | sim: режимы сбоев (потери пакетов, error, таймауты) | 📂 Открыта | 3 |
| TEL-00013 | Купить батарею и первый реальный полёт по чек-листу | ❗ Владелец | 2 |
| TEL-00028 | core: иерархия дронов база → семейство → модель | ✅ Выполнена | 8 |
| TEL-00029 | sim: модель дрона в симуляторе (tello / tello-edu) | ✅ Выполнена | 3 |

## 📝 Детали задач

### 🔧 Build & Infrastructure

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

### [TEL-00006] core: keepalive и автопосадка при низком заряде
- **Status:** ✅ Выполнена
- **Complexity:** 3
- **Description:** Дрон сам садится через 15 с без команд. Фоновый keepalive (`command` раз в ~5 с,
  если не было других команд). По телеметрии `bat` ≤ порога (по умолчанию 15 %) — предупреждение,
  ≤ 10 % — `land`. Проверка на `tello-sim` (его разряд батареи ускоряемый).

### [TEL-00028] core: иерархия дронов база → семейство → модель
- **Status:** ✅ Выполнена
- **Complexity:** 8
- **Description:** Три уровня, как слои OSI; фреймворк управления (CLI, миссии, keepalive) знает
  только базу и не знает, какой дрон за ней.
  1. `tello::DroneBase` (переименовать namespace/библиотеку — решить в задаче) — абстрактная база:
     общие для любого дрона действия (`connect`, `takeoff`, `land`, `emergency`, `move`, `rotate`,
     `battery`, `height`, `state`…) с дефолтными заглушками → `NotSupported`; `capabilities()` —
     набор того, что модель умеет, чтобы CLI показывал в `help` только доступное.
  2. Семейство: `TelloFamily : DroneBase` — общий протокол Ryze/DJI Tello (текст по UDP 8889,
     телеметрия 8890, диапазоны SDK). Сюда переезжает нынешний `Drone` + `ITransport`.
     Другие производители потом — соседние семейства.
  3. Модель: `TelloClassic : TelloFamily` — дрон владельца (старый Tello, прошивка без обновлений,
     SDK 1.3: нет `stop`, `sdk?`, `sn?`, mission pads — проверить на дроне в TEL-00013);
     `TelloEdu : TelloFamily` (SDK 2.0) — заготовка под TEL-00023.
  Фабрика `make_drone(model, transport)`, флаг CLI `--model tello|tello-edu` (по умолчанию `tello`).
  Тесты: команды, которых нет у модели, кидают `NotSupported` и ничего не шлют.

### 💻 CLI

### [TEL-00005] cli: REPL на C++ (паритет с Rust)
- **Status:** ✅ Выполнена
- **Complexity:** 8
- **Description:** Перенос `legacy/rust/src/command_line.rs`: реестр команд с категориями, `help`,
  `version`, `info`, `wait`, `exit`, несколько команд через `;`, история и автодополнение
  (`replxx` через FetchContent), задержки по командам заменить ожиданием ответа `ok`.
  Флаги `--host`, `--port`, `--state-port` — для запуска против симулятора. Без несуществующих
  медиа-команд (см. AGENTS.md → SDK facts). Позиционные команды (`camera_to_center` и т.п.) — TEL-00021.
  Работает только через `DroneBase`; `help` строится из `capabilities()` модели.
- **Блокирована:** TEL-00028

### [TEL-00007] cli: команды безопасности emergency / stop
- **Status:** ✅ Выполнена
- **Complexity:** 2
- **Description:** `emergency` (выключить моторы, без подтверждения — это аварийная кнопка),
  `hover` → `stop`. Горячая клавиша Ctrl+C в полёте → `land`, не выход из программы.

### [TEL-00008] cli: миссии из файла
- **Status:** ✅ Выполнена
- **Complexity:** 3
- **Description:** `mission <file>` и `tello --mission <file>`: команды CLI построчно, `#` — комментарии,
  остановка и `land` при первой ошибке. `square <size>` как встроенный пример. Тест на симуляторе.

### 🧪 Simulator

### [TEL-00012] sim: режимы сбоев (потери пакетов, error, таймауты)
- **Status:** Открыта
- **Complexity:** 3
- **Description:** Флаги `tello-sim`: `--drop N%` (потеря ответов), `--delay MS`, `--fail-cmd <name>`
  (всегда `error`), `--battery N`. Тесты: ретраи и таймауты транспорта, поведение CLI при ошибке.

### [TEL-00029] sim: модель дрона в симуляторе (tello / tello-edu)
- **Status:** ✅ Выполнена
- **Complexity:** 3
- **Description:** `tello-sim --model tello|tello-edu`: классический Tello (SDK 1.3) отвечает на
  `stop`/`sdk?`/`sn?` как старая прошивка, телеметрия без полей mission pad (`mid`, `x`, `y`, `z`, `mpry`).
  Интеграционные тесты TEL-00028 гоняют обе модели.
- **Блокирована:** TEL-00028

### 🚁 Owner

### [TEL-00013] Купить батарею и первый реальный полёт по чек-листу
- **Status:** ❗ Требует действий владельца
- **Complexity:** 2
- **Description:** Батарея заказывается. Первый полёт: открытое место без ветра, `battery?`, `takeoff`,
  `up 50`, `cw 90`, `land`; затем keepalive (постоять 20 с в воздухе) и `emergency` на высоте < 30 см.
  Результаты — новыми задачами в Sandbox.

---

## 📝 User Sandbox (Писать сюда свои задачи)
