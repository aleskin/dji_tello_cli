# DJI Tello CLI — uputstvo za korisnike

[English](../README.md) · [Русский](manual.ru.md) · **Srpski**

`tello` je interaktivni program za komandnu liniju za upravljanje dronom DJI / Ryze Tello sa
računara pod Linuksom: komande, figure sa fotografijama i videom, misije iz fajla, letenje
tastaturom i gejmpedom. Uz njega dolazi simulator `tello-sim`: sve može da se isproba bez drona.

## 1. Instalacija

```bash
sudo apt install cmake g++ googletest libavcodec-dev libavformat-dev libavutil-dev libsdl2-dev
make build      # build/tello i build/tello-sim
make test       # provera: testovi protiv simulatora
```

Gotovi paketi: `make package` → `build-package/tello_<verzija>_amd64.deb` i `.rpm`.

## 2. Probajte bez drona

```bash
make run-sim    # simulator na 127.0.0.1:8889 i program povezan sa njim
```

Ili u dva terminala:

```bash
make sim                                     # terminal 1: lažni dron, ispisuje svaku komandu
./build/tello --host 127.0.0.1 --port 8889   # terminal 2
```

Simulator odgovara kao pravi dron, prati visinu, položaj i pražnjenje baterije, sam sleće posle
15 s tišine, a posle `streamon` šalje probnu sliku kamere. Pre prvog pravog leta isprobajte na
njemu sve što planirate.

## 3. Prvi let

1. Uključite dron i povežite se na njegovu Wi-Fi mrežu (`TELLO-XXXXXX`).
2. Pokrenite `./build/tello` (za Tello EDU: `./build/tello --model tello-edu`).
3. Ukucajte `help`.

```
tello> takeoff; up 50; cw 90
tello> battery
tello> land
```

Više komandi se piše u jednom redu, razdvojeno sa `;`; red se zaustavlja na prvoj grešci.
`Tab` dopunjuje komande, strelice listaju istoriju.

Opcije pokretanja: `--host`, `--port`, `--state-port` (0 — bez telemetrije), `--video-port`,
`--model`, `--mission FAJL`.

## 4. Bezbednost

- **Ctrl+C u vazduhu — sletanje.** Program se nikad ne zatvara dok je dron u vazduhu.
  Na zemlji Ctrl+C samo briše red; izlaz je `exit` ili Ctrl+D.
- **Keepalive:** dron sam sleće posle 15 s bez komandi, zato mu program šalje signal dok ćutite.
- **Baterija:** upozorenje na 15 %, automatsko sletanje na 10 % (podešava se).
- **`emergency`** odmah gasi motore, bez potvrde — dron pada. Samo za krajnju nuždu.
- Letite iznad meke podloge, daleko od ljudi i drveća; prve figure probajte sa `dry` (vidi dole).

## 5. Komande

`help` prikazuje samo ono što izabrani model ume; `help <komanda>` daje detalje.

| Grupa | Komande |
|---|---|
| Sistem | `help [cmd]`, `version`, `info`, `wait <s>`, `alias`, `aliases`, `unalias <ime>`, `exit` |
| Let | `connect`, `takeoff`, `land`, `hover` (EDU), `emergency` |
| Kretanje | `forward/back/left/right/up/down <20..500 cm>`, `cw/ccw <1..360°>`, `flip <l\|r\|f\|b>`, `speed <10..100 cm/s>`, `go <x> <y> <z> <brzina>`, `return` / `home` (povratak iznad tačke poletanja i sletanje) |
| Kamera ka tački | `camera_to_center <x> <y>`, `camera_from_center <x> <y>`, `orbit <50..500 cm> [cw\|ccw] [brzina]` (kruženje oko tačke ispred, u 12 deonica) |
| Misije | `mission <fajl>`, `square <cm>`, `record start <fajl>\|stop\|status` |
| Tastatura | `keyboard` / `kb`, `kbspeed [10..100]` |
| Gejmped | `joystick` / `js` |
| Video i foto | `video start\|stop\|show\|hide\|status`, `photo` |
| Telemetrija | `battery`, `height`, `wifi`, `get_speed`, `temp`, `time`, `attitude`, `baro`, `tof`, `sdk` (EDU), `sn` (EDU), `state`, `position` / `pos` |

`takeoff` nema visinu: poletanje (oko 80 cm), pa `up <cm>`.

## 6. Figure

Figura je gotov let jednom komandom: poletanje, figura, povratak u centar, sletanje. Svi
parametri su neobavezni i idu redom; reči (`cw`, `in`, `flat`...) mogu da stoje bilo gde.
Jedinice: centimetri, stepeni, cm/s.

- **`dry`** na kraju bilo koje figure — ništa se ne šalje dronu, prikazuje se spisak komandi,
  broj fotografija, vreme, najveća visina i potrošnja baterije. Na primer: `flower 300 150 dry`.
- **`video`** — snima video celog leta u `~/Videos/tello/`.
- Fotografije figure idu u poseban folder `~/Pictures/tello/<figura>_DATUM_VREME/01.jpg, 02.jpg...`
- Pre poletanja program proverava da li ima slike sa kamere; bez nje ne poleće.
- Ako komanda u vazduhu ne uspe — figura se prekida i dron sleće.

| Figura | Šta radi | Podrazumevano |
|---|---|---|
| `flower [visina] [poluprečnik] [korak] [in\|out] [cw\|ccw] [brzina]` | horizontalni krug oko tačke poletanja, fotografija na svakih `korak` stepeni; kamera `in` — ka centru, `out` — ka spolja | 200, 100, 30°, in, cw, 30 |
| `wheel [visina] [poluprečnik] [korak] [cw\|ccw] [in\|out] [brzina]` | vertikalni krug popreko vašeg pogleda (levo-desno), počinje odozgo; `cw` — prvo desno; visina mora biti veća od poluprečnika | 200, 100, 30°, cw, in |
| `loop [visina] [poluprečnik] [korak] [away\|toward] [in\|out\|side] [brzina]` | vertikalni krug od vas i nazad (napred-nazad); kamera ka vama, napred ili u stranu | 200, 100, 30°, away, in |
| `panorama [visina] [korak] [cw\|ccw]` | okretanje u mestu sa fotografijom na svakom koraku — kružna panorama | 200, 30° |
| `tower [od] [do] [korak]` | penjanje pravo gore sa fotografijom na svakoj visini (fasada, drvo) | 100..500 na 50 |
| `dronie [rastojanje] [visina] [brzina]` | video: kamera na vas, dron se udaljava unazad i gore dijagonalno, pa se vraća | 300, 200 |
| `reveal [rastojanje] [visina] [brzina]` | video: kreće nisko (50 cm), leti napred i gore — pogled se „otvara“ | 300, 200 |
| `bounce [puta] [raspon] [cw\|ccw] [brzina]` | njihanje gore-dole; sa `cw`/`ccw` usput i pun okret | 3 puta, 100 |
| `flips [l\|r\|f\|b ...]` | serija saltoa na 150 cm; potrebno najmanje 50 % baterije | sva četiri |
| `helix [navoja] [od vis.] [do vis.] [od polupr.] [do polupr.] [korak] [cw\|ccw] [brzina]` | video: spirala oko tačke poletanja nagore i ka spolja, kamera ka centru | 2 navoja, 100..300, 100..200 |
| `boomerang [dužina] [širina] [uspon] [cw\|ccw] [brzina]` | video: ovalna putanja oko tačke poletanja sa usponom na dalekom kraju, kamera ka centru | 400, 200, 100 |
| `eight [poluprečnik] [visina] [korak] [flat\|upright] [brzina]` | video: osmica, horizontalna (`flat`) ili vertikalna (`upright`) | 100, 200, flat |
| `polygon <uglova> [poluprečnik] [visina] [flat\|upright] [brzina]` | mnogougao sa 3..12 uglova u vazduhu | 100, 200, flat |
| `star [poluprečnik] [visina] [flat\|upright] [brzina]` | petokraka zvezda | 100, 200, flat |
| `rings [poluprečnik] [od vis.] [do vis.] [prstenova] [korak] [brzina]` | nekoliko krugova fotografija na različitim visinama, kamera ka centru, sve u jedan folder — za 3D model (Meshroom i sl.) | 150, 100..300, 3 prstena |
| `timelapse [interval s] [kadrova] [visina]` | lebdi i snima kadar na svakih N sekundi; posle sletanja spaja kadrove u `timelapse.mp4` (10 kadrova/s) | 2 s, 30 kadrova, 200 |
| `hyperlapse [rastojanje] [kadrova] [visina] [brzina]` | polako leti napred snimajući kadrove, vraća se; spaja u `hyperlapse.mp4` | 500, 20 kadrova, 200, 20 |
| `scan [širina] [od vis.] [do vis.] [korak] [brzina]` | okrenut ka zidu: redovi fotografija u obliku zmije (red, uspon, red nazad) sa preklapanjem | 300, 100..300, 50 |

Primeri:

```
tello> flower dry                    # pogledajte plan
tello> flower 150 200 45 out         # krug poluprečnika 2 m na 1,5 m, fotografije ka spolja na 45°
tello> wheel 250 100 ccw video       # točak suprotno od kazaljke, sa videom celog leta
tello> timelapse 5 60                # 60 kadrova na svakih 5 s, pa timelapse.mp4
```

## 7. Video i foto

- `video show` — prozor sa živom slikom i podacima na ekranu (baterija, visina, kurs); `video hide` — zatvara ga.
- `video start` / `video stop` — snimak u `~/Videos/tello/tello_DATUM_VREME.mp4` (bez ponovnog kodiranja).
- `photo` — kadar sa kamere u `~/Pictures/tello/`. Tello nema memoriju za fotografije: snimak se
  uzima iz video toka na računaru.

## 8. Tastatura i gejmped

- **`keyboard`** (u vazduhu): upravljanje tasterima u video prozoru — `W/A/S/D` kretanje,
  strelice gore/dole — visina, levo/desno — okretanje, `Esc` — nazad na komande.
  `kbspeed 60` — jačina „palica“ (podrazumevano 40).
- **`joystick`** (i na zemlji): desna palica — kretanje, leva — visina i okretanje,
  `A` — poletanje, `B` — sletanje, `Back`+`Start` zajedno — hitno gašenje motora,
  `Esc` u prozoru ili `Guide` — nazad na komande.

## 9. Misije i snimanje putanje

Misija je tekstualni fajl, jedan red komandi po redu, `#` je komentar. Na prvoj grešci misija se
zaustavlja i dron sleće.

```
# mission.txt
takeoff
up 50
square 100
land
```

```bash
./build/tello --mission mission.txt    # izlazni kod 0 samo ako su svi redovi uspeli
```

Unutar programa: `mission mission.txt`.

**Snimanje putanje:** `record start route.txt` — sve komande leta, kretanja, figura i kamere
upisuju se u fajl do `record stop`. Let tastaturom ili gejmpedom upisuje se kao jedna komanda `go`
i okret, prema izračunatom položaju. `mission route.txt` ponavlja putanju. `record status` —
koliko je redova već upisano.

## 10. Sopstvene komande (alijasi)

```
tello> alias hop up $1; down $1      # $1..$9 — argumenti, $* — svi
tello> takeoff; hop 50; land
tello> aliases                       # spisak; unalias hop — brisanje
```

Alijasi se čuvaju u `~/.config/tello/aliases` i rade i u misijama.

## 11. Podešavanja

Fajl `~/.config/tello/config`; opcije komandne linije imaju prednost.

```
host = 192.168.10.1
port = 8889
state_port = 8890        # 0 — bez telemetrije
video_port = 11111
model = tello            # ili tello-edu
warn_battery = 15        # procenti
land_battery = 10
kbspeed = 40             # tastatura, 10..100
js_deadzone = 10         # gejmped, mrtva zona palice u procentima, 0..50
key_forward = w          # i key_back/left/right/up/down/turn_left/turn_right
```

## 12. Šta treba znati o Tellu

- **Kamera gleda samo napred i ne naginje se.** Tačka poletanja je u kadru samo ako je
  poluprečnik veći od otprilike dve visine. `loop` prolazi `poluprečnik` cm iza tačke poletanja —
  stanite sa strane ili dalje unazad.
- **Nema GPS-a.** Položaj se računa „po proceni“ — iz izvršenih komandi i brzine iz telemetrije.
  Greška raste sa svakim metrom; `return` nije GPS povratak kući, već prave deonice po toj proceni.
- Pomeranja kraća od 20 cm dron ne izvršava; jedna komanda — najviše 500 cm.
- Salto samo kad je baterija iznad 50 %.
