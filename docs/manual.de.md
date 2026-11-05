# DJI Tello CLI — Benutzerhandbuch

[English](../README.md) · [Русский](manual.ru.md) · [Srpski](manual.sr.md) · **Deutsch** · [Français](manual.fr.md) · [Español](manual.es.md)

`tello` ist ein interaktives Kommandozeilenprogramm, mit dem man die Drohne DJI / Ryze Tello von
einem Linux-Rechner aus steuert: Befehle, Figuren mit Fotos und Video, Missionen aus einer Datei,
Fliegen mit Tastatur und Gamepad. Dazu gehört der Simulator `tello-sim`: Alles lässt sich ohne
Drohne ausprobieren.

## 1. Installation

```bash
sudo apt install cmake g++ googletest libavcodec-dev libavformat-dev libavutil-dev libsdl2-dev
make build      # build/tello und build/tello-sim
make test       # Prüfung: Tests gegen den Simulator
```

Fertige Pakete: `make package` → `build-package/tello_<Version>_amd64.deb` und `.rpm`.

## 2. Ohne Drohne ausprobieren

```bash
make run-sim    # Simulator auf 127.0.0.1:8889 und das Programm, mit ihm verbunden
```

Oder in zwei Terminals:

```bash
make sim                                     # Terminal 1: die falsche Drohne, zeigt jeden Befehl
./build/tello --host 127.0.0.1 --port 8889   # Terminal 2
```

Der Simulator antwortet wie die echte Drohne, verfolgt Höhe, Position und Akkuverbrauch, landet
nach 15 s Stille von selbst und sendet nach `streamon` ein Testbild der Kamera. Spielen Sie vor
dem ersten echten Flug alles darauf durch, was Sie vorhaben.

## 3. Erster Flug

1. Drohne einschalten und mit ihrem WLAN verbinden (`TELLO-XXXXXX`).
2. `./build/tello` starten (für die Tello EDU: `./build/tello --model tello-edu`).
3. `help` eingeben.

```
tello> takeoff; up 50; cw 90
tello> battery
tello> land
```

Mehrere Befehle stehen in einer Zeile, getrennt durch `;`; die Zeile stoppt beim ersten Fehler.
`Tab` ergänzt Befehle, die Pfeiltasten blättern im Verlauf.

Startoptionen: `--host`, `--port`, `--state-port` (0 — ohne Telemetrie), `--video-port`,
`--model`, `--mission DATEI`, `--log-dir ORDNER`, `--no-log`.

## 4. Sicherheit

- **Strg+C in der Luft — Landung.** Das Programm beendet sich nie, solange die Drohne fliegt.
  Am Boden löscht Strg+C nur die Zeile; beenden mit `exit` oder Strg+D.
- **Keepalive:** Die Drohne landet nach 15 s ohne Befehl von selbst, deshalb sendet das Programm
  ihr ein Signal, solange Sie nichts eingeben.
- **Akku:** Warnung bei 15 %, automatische Landung bei 10 % (einstellbar).
- **`emergency`** schaltet die Motoren sofort und ohne Rückfrage ab — die Drohne fällt. Nur im Notfall.
- Fliegen Sie über weichem Untergrund, fern von Menschen und Bäumen; neue Figuren zuerst mit `dry` (siehe unten).

## 5. Befehle

`help` zeigt nur, was das gewählte Modell kann; `help <Befehl>` zeigt Details.

| Gruppe | Befehle |
|---|---|
| System | `help [cmd]`, `version`, `info`, `wait <s>`, `alias`, `aliases`, `unalias <Name>`, `exit`, `log` (Pfad des Sitzungsprotokolls) |
| Flug | `connect`, `takeoff`, `land`, `hover` (EDU), `emergency` |
| Bewegung | `forward/back/left/right/up/down <20..500 cm>`, `cw/ccw <1..360°>`, `flip <l\|r\|f\|b>`, `speed <10..100 cm/s>`, `go <x> <y> <z> <Tempo>`, `return` / `home` (zurück über den Startpunkt und landen) |
| Kamera auf einen Punkt | `camera_to_center <x> <y>`, `camera_from_center <x> <y>`, `orbit <50..500 cm> [cw\|ccw] [Tempo]` (Kreis um einen Punkt voraus, in 12 Abschnitten) |
| Missionen | `mission <Datei>`, `square <cm>`, `record start <Datei>\|stop\|status` |
| Tastatur | `keyboard` / `kb`, `kbspeed [10..100]` |
| Gamepad | `joystick` / `js` |
| Video und Foto | `video start\|stop\|show\|hide\|status`, `photo` |
| Telemetrie | `battery`, `height`, `wifi`, `get_speed`, `temp`, `time`, `attitude`, `baro`, `tof`, `sdk` (EDU), `sn` (EDU), `state`, `position` / `pos` |

`takeoff` hat keine Höhe: Start (etwa 80 cm), danach `up <cm>`.

## 6. Figuren

Eine Figur ist ein fertiger Flug mit einem Befehl: Start, Figur, zurück zur Mitte, Landung. Alle
Parameter sind optional und folgen der Reihe nach; Wörter (`cw`, `in`, `flat`...) dürfen an
beliebiger Stelle stehen. Einheiten: Zentimeter, Grad, cm/s.

- **`dry`** am Ende jeder Figur — nichts wird an die Drohne gesendet; angezeigt werden die Befehle,
  die Zahl der Fotos, die Dauer, die größte Höhe und der Akkuverbrauch. Beispiel: `flower 300 150 dry`.
- **`video`** — nimmt den ganzen Flug als Video in `~/Videos/tello/` auf.
- Die Fotos einer Figur landen in einem eigenen Ordner `~/Pictures/tello/<Figur>_DATUM_ZEIT/01.jpg, 02.jpg...`
- Vor dem Start prüft das Programm, ob ein Kamerabild da ist; ohne Bild startet es nicht.
- Schlägt ein Befehl in der Luft fehl, wird die Figur abgebrochen und die Drohne landet.

| Figur | Was sie tut | Standard |
|---|---|---|
| `flower [Höhe] [Radius] [Schritt] [in\|out] [cw\|ccw] [Tempo]` | waagerechter Kreis um den Startpunkt, ein Foto alle `Schritt` Grad; Kamera `in` — zur Mitte, `out` — nach außen | 200, 100, 30°, in, cw, 30 |
| `wheel [Höhe] [Radius] [Schritt] [cw\|ccw] [in\|out] [Tempo]` | senkrechter Kreis quer zu Ihrem Blick (links-rechts), beginnt oben; `cw` — zuerst nach rechts; die Höhe muss größer als der Radius sein | 200, 100, 30°, cw, in |
| `loop [Höhe] [Radius] [Schritt] [away\|toward] [in\|out\|side] [Tempo]` | senkrechter Kreis von Ihnen weg und zurück (vor-zurück); Kamera auf Sie, nach vorn oder zur Seite | 200, 100, 30°, away, in |
| `panorama [Höhe] [Schritt] [cw\|ccw]` | Drehung auf der Stelle mit einem Foto bei jedem Schritt — Rundumpanorama | 200, 30° |
| `tower [von] [bis] [Schritt]` | senkrechter Aufstieg mit einem Foto auf jeder Höhe (Fassade, Baum) | 100..500 in 50er-Schritten |
| `dronie [Abstand] [Höhe] [Tempo]` | Video: Kamera auf Sie, die Drohne fliegt schräg nach hinten und oben weg und kommt zurück | 300, 200 |
| `reveal [Abstand] [Höhe] [Tempo]` | Video: beginnt tief (50 cm), fliegt nach vorn und oben — die Aussicht „öffnet sich“ | 300, 200 |
| `bounce [Mal] [Hub] [cw\|ccw] [Tempo]` | auf und ab; mit `cw`/`ccw` dabei eine volle Drehung | 3-mal, 100 |
| `flips [l\|r\|f\|b ...]` | Saltoserie auf 150 cm; mindestens 50 % Akku nötig | alle vier |
| `helix [Windungen] [von Höhe] [bis Höhe] [von Radius] [bis Radius] [Schritt] [cw\|ccw] [Tempo]` | Video: Spirale um den Startpunkt nach oben und außen, Kamera zur Mitte | 2 Windungen, 100..300, 100..200 |
| `boomerang [Länge] [Breite] [Anstieg] [cw\|ccw] [Tempo]` | Video: Oval um den Startpunkt, am fernen Ende höher, Kamera zur Mitte | 400, 200, 100 |
| `eight [Radius] [Höhe] [Schritt] [flat\|upright] [Tempo]` | Video: eine Acht, waagerecht (`flat`) oder senkrecht (`upright`) | 100, 200, flat |
| `polygon <Ecken> [Radius] [Höhe] [flat\|upright] [Tempo]` | Vieleck mit 3..12 Ecken in der Luft | 100, 200, flat |
| `star [Radius] [Höhe] [flat\|upright] [Tempo]` | fünfzackiger Stern | 100, 200, flat |
| `rings [Radius] [von Höhe] [bis Höhe] [Ringe] [Schritt] [Tempo]` | mehrere Fotokreise auf verschiedenen Höhen, Kamera zur Mitte, alles in einem Ordner — für ein 3D-Modell (Meshroom u. ä.) | 150, 100..300, 3 Ringe |
| `timelapse [Intervall s] [Bilder] [Höhe]` | schwebt und nimmt alle N Sekunden ein Bild auf; nach der Landung werden die Bilder zu `timelapse.mp4` (10 Bilder/s) zusammengefügt | 2 s, 30 Bilder, 200 |
| `hyperlapse [Abstand] [Bilder] [Höhe] [Tempo]` | fliegt langsam nach vorn und nimmt Bilder auf, kommt zurück; fügt sie zu `hyperlapse.mp4` zusammen | 500, 20 Bilder, 200, 20 |
| `scan [Breite] [von Höhe] [bis Höhe] [Schritt] [Tempo]` | vor einer Wand: Fotoreihen in Schlangenlinien (Reihe, höher, Reihe zurück) mit Überlappung | 300, 100..300, 50 |

Beispiele:

```
tello> flower dry                    # den Plan ansehen
tello> flower 150 200 45 out         # Kreis mit 2 m Radius in 1,5 m Höhe, Fotos nach außen alle 45°
tello> wheel 250 100 ccw video       # Rad gegen den Uhrzeigersinn, mit Video des ganzen Flugs
tello> timelapse 5 60                # 60 Bilder alle 5 s, dann timelapse.mp4
```

## 7. Video und Foto

- `video show` — Fenster mit Livebild und Einblendungen (Akku, Höhe, Kurs); `video hide` schließt es.
- `video start` / `video stop` — Aufnahme nach `~/Videos/tello/tello_DATUM_ZEIT.mp4` (ohne Neukodierung).
- `photo` — ein Kamerabild nach `~/Pictures/tello/`. Die Tello hat keinen Fotospeicher: Das Bild
  wird auf dem Rechner aus dem Videostrom genommen.

## 8. Tastatur und Gamepad

- **`keyboard`** (in der Luft): Steuerung mit den Tasten im Videofenster — `W/A/S/D` Bewegung,
  Pfeil hoch/runter — Höhe, links/rechts — Drehung, `Esc` — zurück zu den Befehlen.
  `kbspeed 60` — Stärke der „Knüppel“ (Standard 40).
- **`joystick`** (auch am Boden): rechter Stick — Bewegung, linker — Höhe und Drehung,
  `A` — Start, `B` — Landung, `Back`+`Start` zusammen — Notabschaltung der Motoren,
  `Esc` im Fenster oder `Guide` — zurück zu den Befehlen.

## 9. Missionen und Routenaufzeichnung

Eine Mission ist eine Textdatei mit einer Befehlszeile pro Zeile, `#` leitet einen Kommentar ein.
Beim ersten Fehler stoppt die Mission und die Drohne landet.

```
# mission.txt
takeoff
up 50
square 100
land
```

```bash
./build/tello --mission mission.txt    # Exit-Code 0 nur, wenn alle Zeilen geklappt haben
```

Im Programm: `mission mission.txt`.

**Routenaufzeichnung:** `record start route.txt` — alle Flug-, Bewegungs-, Figuren- und
Kamerabefehle werden bis `record stop` in die Datei geschrieben. Ein Flug mit Tastatur oder Gamepad
wird als ein `go`-Befehl plus Drehung nach der berechneten Position gespeichert.
`mission route.txt` fliegt die Route erneut. `record status` — wie viele Zeilen schon aufgezeichnet sind.

## 10. Eigene Befehle (Aliase)

```
tello> alias hop up $1; down $1      # $1..$9 — Argumente, $* — alle
tello> takeoff; hop 50; land
tello> aliases                       # Liste; unalias hop — löschen
```

Aliase werden in `~/.config/tello/aliases` gespeichert und funktionieren auch in Missionen.

## 11. Einstellungen

Datei `~/.config/tello/config`; Kommandozeilenoptionen haben Vorrang.

```
host = 192.168.10.1
port = 8889
state_port = 8890        # 0 — ohne Telemetrie
video_port = 11111
model = tello            # oder tello-edu
warn_battery = 15        # Prozent
land_battery = 10
kbspeed = 40             # Tastatur, 10..100
js_deadzone = 10         # Gamepad, tote Zone des Sticks in Prozent, 0..50
log_dir = ~/.local/state/tello/logs   # Sitzungsprotokolle; "off" = kein Protokoll
log_keep = 30            # so viele neueste Protokolle behalten
key_forward = w          # und key_back/left/right/up/down/turn_left/turn_right
```

## 12. Flugprotokoll

Jeder Start schreibt ein Textprotokoll für die Auswertung nach dem Flug, eine Datei pro Sitzung:
`~/.local/state/tello/logs/tello_JJJJMMTT_HHMMSS.log` (oder `$XDG_STATE_HOME/tello/logs`),
die neuesten 30 bleiben erhalten. `log` zeigt die aktuelle Datei; `--log-dir ORDNER` oder
`log_dir` ändern den Ort, `--no-log` oder `log_dir = off` schalten das Protokoll ab. Einträge
werden sofort geschrieben — ein Absturz verliert nichts.

Was drinsteht:
- **Sitzungskopf**: Version, Befehlszeile, Modell, Adresse und Ports, Akkugrenzen;
- **jede Eingabezeile und jeder Befehl** mit `begin` / `end ...: ok|failed: <Grund>|exception, N ms` —
  ein `begin` ohne `end` heißt: der Befehl hing oder das Programm starb darin;
- **jeder SDK-Befehl und jede Antwort** (`> takeoff`, `< ok  [takeoff, 2140 ms]`), Zeitüberschreitungen,
  verspätete Antworten; wiederholte `rc`-Befehle werden gezählt, nicht einzeln aufgeführt;
- **Position** nach jedem Flugbefehl, **Telemetrie** jede Sekunde in der Luft (alle 10 s am
  Boden), `telemetry lost` / `telemetry back`;
- **alles, was auf dem Bildschirm stand** (`console: ...`), Akkuwarnungen, Ereignisse des
  Videostroms, Fotos und Aufnahmen, Strg+C, Fehler und unerwartete Ausnahmen, fatale Signale
  (`*** FATAL signal N`).

Eine Zeile sieht so aus: `2026-11-05 09:24:20.387 INFO  main     drone: > takeoff`
(Zeit, Stufe `DEBUG/INFO/WARN/ERROR`, Thread, Komponente). Probleme finden:
`grep -E 'WARN|ERROR|FATAL' ~/.local/state/tello/logs/tello_*.log`.

## 13. Was man über die Tello wissen sollte

- **Die Kamera schaut nur nach vorn und lässt sich nicht neigen.** Der Startpunkt ist nur im Bild,
  wenn der Radius größer als etwa die doppelte Höhe ist. `loop` fliegt `Radius` cm hinter dem
  Startpunkt durch — stellen Sie sich zur Seite oder weiter nach hinten.
- **Kein GPS.** Die Position wird geschätzt (Koppelnavigation) — aus den ausgeführten Befehlen und
  der Geschwindigkeit aus der Telemetrie. Der Fehler wächst mit jedem Meter; `return` ist keine
  GPS-Rückkehr, sondern gerade Strecken nach dieser Schätzung.
- Bewegungen unter 20 cm führt die Drohne nicht aus; ein Befehl höchstens 500 cm.
- Saltos nur bei mehr als 50 % Akku.
