# DJI Tello CLI — Backlog (next sprint)

Ideas and deferred tasks. The owner moves tasks from here into `TODO.md` at sprint start.
Task numbers come from the `Next Task ID` counter in `TODO.md`.
Source: the old `ROADMAP.md` of the Rust prototype (items that relied on non-existent SDK
commands — drone-side photo storage, `exposure`, `jpeg` — were dropped or rewritten).

## 📝 Задачи

### [TEL-00023] core: Tello EDU — mission pads и режим точки доступа
- **Status:** Закрыта (EDU покупать не планируется)
- **Complexity:** 8
- **Description:** `mon`/`moff`/`mdirection`, `go x y z speed mid`, `jump`; `ap ssid pass` для
  подключения дрона к домашней сети. Только для EDU — определять по `sdk?`.

### [TEL-00025] Рой: несколько дронов одной модели
- **Status:** Идея
- **Complexity:** 21
- **Description:** Перспектива: рой из дронов одной модели, синхронная хореография из файла миссии.
  Классический Tello сам раздаёт Wi-Fi и всегда живёт на `192.168.10.1`, в чужую сеть не
  подключается (это умеет только EDU, `ap ssid pass`). Значит, для роя классических Tello — по
  Wi-Fi-адаптеру на каждый дрон и привязка сокета к интерфейсу (`SO_BINDTODEVICE`). Решить, когда
  появится второй дрон.

### [TEL-00033] gui: дизайн — GUI-приложение и командная строка
- **Status:** Идея
- **Complexity:** 5
- **Description:** Владелец думает: GUI-приложение (видео, телеметрия, управление с клавиатуры/
  геймпада) плюс командная строка для команд. Дизайн впереди: какой тулкит (SDL2 + Dear ImGui,
  Qt), один процесс или GUI поверх CLI, что где живёт. Итог — решение и задачи на реализацию.

### [TEL-00034] core: спутниковый источник позиции (GPS/ГЛОНАСС)
- **Status:** Идея
- **Complexity:** 8
- **Description:** `GnssPosition : PositionSource` для дронов со спутниковым приёмником: позиция
  в координатах + перевод в локальные метры от точки взлёта, качество фикса. У Tello приёмника
  нет — ждёт дрона, у которого он есть.
- **Блокирована:** TEL-00020

### [TEL-00035] core: коррекция позиции по камере
- **Status:** Идея
- **Complexity:** 21
- **Description:** Уточнение счисления по видео (визуальная одометрия, OpenCV): дрейф по
  командам накапливается, камера его поправляет. Исследовательская задача.
- **Блокирована:** TEL-00020, TEL-00014

### [TEL-00026] Компьютерное зрение: трекинг объекта и follow-me
- **Status:** Идея
- **Complexity:** 34
- **Description:** OpenCV по кадрам из TEL-00014, удержание объекта в центре через `rc`.

### [TEL-00027] Панорама: склейка фото
- **Status:** Идея
- **Complexity:** 13
- **Description:** Серия `cw 30` + кадр, склейка OpenCV Stitcher.
