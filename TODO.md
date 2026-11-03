# DJI Tello CLI — TODO

Current sprint. Completed tasks are removed from here at the next version bump (CHANGELOG keeps the history).
Backlog: `TODO-next-sprint.md`. Rules: `AGENTS.md`.

---

## ⚡ Next Task ID: TEL-00036

> Единственный счётчик задач. Берём это число как номер новой задачи и сразу инкрементируем.
> Номер никогда не используется повторно.

---

## 📋 Доска задач
### 🏃 Текущий спринт

| ID | Задача | Статус | Сл. |
|---|---|---|---|
| TEL-00018 | cli: геймпад / джойстик | ✅ Выполнена | 5 |
| TEL-00021 | cli: позиционные фигуры (camera_to_center, круг) | Открыта | 5 |
| TEL-00022 | build: пакеты .deb и .rpm | Открыта | 5 |
| TEL-00013 | Купить батарею и первый реальный полёт по чек-листу | ❗ Владелец | 2 |

## 📝 Детали задач

### 🔧 Build & Infrastructure

### [TEL-00022] build: пакеты .deb и .rpm
- **Status:** Открыта
- **Complexity:** 5
- **Description:** CPack: `make package` → `.deb`/`.rpm` с `tello` и `tello-sim`.

### 🛰️ Core

### 💻 CLI

### [TEL-00018] cli: геймпад / джойстик
- **Status:** ✅ Выполнена
- **Complexity:** 5
- **Description:** Режим `joystick` через SDL2 GameController, мёртвая зона, кнопки takeoff/land/emergency.

### [TEL-00021] cli: позиционные фигуры (camera_to_center, круг)
- **Status:** Открыта
- **Complexity:** 5
- **Description:** Перенос `camera_to_center` / `camera_from_center` из Rust; облёт точки по кругу
  через `curve`/`go`, радиус по высоте, вращение во время облёта.

### 🧪 Simulator

### 🎥 Video

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
