# DJI Tello CLI — manual de usuario

[English](../README.md) · [Русский](manual.ru.md) · [Srpski](manual.sr.md) · [Deutsch](manual.de.md) · [Français](manual.fr.md) · **Español**

`tello` es un programa interactivo de línea de comandos para controlar el dron DJI / Ryze Tello
desde un ordenador con Linux: comandos, figuras con fotos y vídeo, misiones desde un archivo,
vuelo con teclado y mando. Incluye el simulador `tello-sim`: todo se puede probar sin dron.

## 1. Instalación

```bash
sudo apt install cmake g++ googletest libavcodec-dev libavformat-dev libavutil-dev libsdl2-dev
make build      # build/tello y build/tello-sim
make test       # comprobación: pruebas contra el simulador
```

Paquetes listos: `make package` → `build-package/tello_<versión>_amd64.deb` y `.rpm`.

## 2. Probar sin dron

```bash
make run-sim    # el simulador en 127.0.0.1:8889 y el programa conectado a él
```

O en dos terminales:

```bash
make sim                                     # terminal 1: el dron falso, imprime cada comando
./build/tello --host 127.0.0.1 --port 8889   # terminal 2
```

El simulador responde como el dron real, calcula la altura, la posición y la descarga de la
batería, aterriza solo tras 15 s de silencio y, después de `streamon`, envía una imagen de prueba
de la cámara. Antes del primer vuelo real, pruebe en él todo lo que piensa hacer.

## 3. Primer vuelo

1. Encienda el dron y conéctese a su Wi-Fi (`TELLO-XXXXXX`).
2. Ejecute `./build/tello` (para el Tello EDU: `./build/tello --model tello-edu`).
3. Escriba `help`.

```
tello> takeoff; up 50; cw 90
tello> battery
tello> land
```

Se pueden poner varios comandos en una línea separados por `;`; la línea se detiene en el primer
error. `Tab` completa los comandos, las flechas recorren el historial.

Opciones de inicio: `--host`, `--port`, `--state-port` (0 — sin telemetría), `--video-port`,
`--model`, `--mission ARCHIVO`, `--log-dir CARPETA`, `--no-log`.

## 4. Seguridad

- **Ctrl+C en el aire — aterrizaje.** El programa nunca se cierra con el dron en el aire.
  En tierra, Ctrl+C solo borra la línea; para salir: `exit` o Ctrl+D.
- **Keepalive:** el dron aterriza solo tras 15 s sin comandos, por eso el programa le envía una
  señal mientras usted no escribe nada.
- **Batería:** aviso al 15 %, aterrizaje automático al 10 % (configurable), repetido cada 3 s hasta que el dron lo confirme.
- **Salida segura:** `exit`, Ctrl+D o el final de una misión con el dron en el aire aterrizan primero
  y salen solo después del aterrizaje. Si falla, el programa sigue abierto (`land` o `emergency`;
  un segundo `exit` sale de todos modos).
- **Aterrizaje de emergencia:** si el programa muere en el aire (fallo, `kill`, terminal o pipe de salida cerrados),
  envía `land` antes y lo anota en el registro. Si un comando se cuelga 45 s, el dron también aterriza.
- No cubierto: `kill -9`, portátil sin batería, pérdida del Wi-Fi — entonces el Tello aterriza solo
  tras 15 s sin comandos.
- **`emergency`** apaga los motores al instante, sin confirmación — el dron cae. Solo en caso extremo.
- Vuele sobre una superficie blanda, lejos de personas y árboles; pruebe primero las figuras con `dry` (ver abajo).

## 5. Comandos

`help` muestra solo lo que sabe hacer el modelo elegido; `help <comando>` da los detalles.

| Grupo | Comandos |
|---|---|
| Sistema | `help [cmd]`, `version`, `info`, `wait <s>`, `alias`, `aliases`, `unalias <nombre>`, `exit`, `log` (ruta del registro de la sesión) |
| Vuelo | `connect`, `takeoff`, `land`, `hover` (EDU), `emergency` |
| Movimiento | `forward/back/left/right/up/down <20..500 cm>`, `cw/ccw <1..360°>`, `flip <l\|r\|f\|b>`, `speed <10..100 cm/s>`, `go <x> <y> <z> <velocidad>`, `return` / `home` (volver sobre el punto de despegue y aterrizar) |
| Cámara hacia un punto | `camera_to_center <x> <y>`, `camera_from_center <x> <y>`, `orbit <50..500 cm> [cw\|ccw] [velocidad]` (vuelta alrededor de un punto delante, en 12 tramos) |
| Misiones | `mission <archivo>`, `square <cm>`, `record start <archivo>\|stop\|status` |
| Teclado | `keyboard` / `kb`, `kbspeed [10..100]` |
| Mando | `joystick` / `js` |
| Vídeo y foto | `video start\|stop\|show\|hide\|status`, `photo` |
| Telemetría | `battery`, `height`, `wifi`, `get_speed`, `temp`, `time`, `attitude`, `baro`, `tof`, `sdk` (EDU), `sn` (EDU), `state`, `position` / `pos` |

`takeoff` no tiene altura: despegue (unos 80 cm) y luego `up <cm>`.

## 6. Figuras

Una figura es un vuelo completo con un solo comando: despegue, figura, regreso al centro,
aterrizaje. Todos los parámetros son opcionales y van en orden; las palabras (`cw`, `in`,
`flat`...) pueden ir en cualquier lugar. Unidades: centímetros, grados, cm/s.

- **`dry`** al final de cualquier figura — no se envía nada al dron; se muestra la lista de
  comandos, el número de fotos, el tiempo, la altura máxima y el consumo de batería.
  Por ejemplo: `flower 300 150 dry`.
- **`video`** — graba en vídeo todo el vuelo en `~/Videos/tello/`.
- Las fotos de una figura van a una carpeta propia `~/Pictures/tello/<figura>_FECHA_HORA/01.jpg, 02.jpg...`
- Antes de despegar, el programa comprueba que hay imagen de la cámara; sin ella no despega.
- Si un comando falla en el aire, la figura se interrumpe y el dron aterriza.

| Figura | Qué hace | Por defecto |
|---|---|---|
| `flower [altura] [radio] [paso] [in\|out] [cw\|ccw] [velocidad]` | círculo horizontal alrededor del punto de despegue, una foto cada `paso` grados; cámara `in` — hacia el centro, `out` — hacia fuera | 200, 100, 30°, in, cw, 30 |
| `wheel [altura] [radio] [paso] [cw\|ccw] [in\|out] [velocidad]` | círculo vertical de través a su mirada (izquierda-derecha), empieza arriba; `cw` — primero a la derecha; la altura debe ser mayor que el radio | 200, 100, 30°, cw, in |
| `loop [altura] [radio] [paso] [away\|toward] [in\|out\|side] [velocidad]` | círculo vertical que se aleja de usted y vuelve (adelante-atrás); cámara hacia usted, hacia delante o de lado | 200, 100, 30°, away, in |
| `panorama [altura] [paso] [cw\|ccw]` | giro en el sitio con una foto en cada paso — panorama circular | 200, 30° |
| `tower [desde] [hasta] [paso]` | subida vertical con una foto en cada altura (fachada, árbol) | 100..500 de 50 en 50 |
| `dronie [distancia] [altura] [velocidad]` | vídeo: cámara hacia usted, el dron se aleja en diagonal hacia atrás y arriba, y vuelve | 300, 200 |
| `reveal [distancia] [altura] [velocidad]` | vídeo: empieza bajo (50 cm), vuela hacia delante y arriba — la vista «se revela» | 300, 200 |
| `bounce [veces] [amplitud] [cw\|ccw] [velocidad]` | subidas y bajadas; con `cw`/`ccw`, además una vuelta completa | 3 veces, 100 |
| `flips [l\|r\|f\|b ...]` | serie de volteretas a 150 cm; hace falta al menos 50 % de batería | las cuatro |
| `helix [vueltas] [desde alt.] [hasta alt.] [desde radio] [hasta radio] [paso] [cw\|ccw] [velocidad]` | vídeo: espiral alrededor del punto de despegue hacia arriba y hacia fuera, cámara al centro | 2 vueltas, 100..300, 100..200 |
| `boomerang [largo] [ancho] [subida] [cw\|ccw] [velocidad]` | vídeo: óvalo alrededor del punto de despegue, más alto en el extremo lejano, cámara al centro | 400, 200, 100 |
| `eight [radio] [altura] [paso] [flat\|upright] [velocidad]` | vídeo: un ocho, horizontal (`flat`) o vertical (`upright`) | 100, 200, flat |
| `polygon <vértices> [radio] [altura] [flat\|upright] [velocidad]` | polígono de 3 a 12 vértices en el aire | 100, 200, flat |
| `star [radio] [altura] [flat\|upright] [velocidad]` | estrella de cinco puntas | 100, 200, flat |
| `rings [radio] [desde alt.] [hasta alt.] [anillos] [paso] [velocidad]` | varios círculos de fotos a distintas alturas, cámara al centro, todo en una carpeta — para un modelo 3D (Meshroom, etc.) | 150, 100..300, 3 anillos |
| `timelapse [intervalo s] [fotogramas] [altura]` | se mantiene en el aire y toma un fotograma cada N segundos; tras aterrizar, une los fotogramas en `timelapse.mp4` (10 fps) | 2 s, 30 fotogramas, 200 |
| `hyperlapse [distancia] [fotogramas] [altura] [velocidad]` | vuela despacio hacia delante tomando fotogramas, vuelve; los une en `hyperlapse.mp4` | 500, 20 fotogramas, 200, 20 |
| `scan [ancho] [desde alt.] [hasta alt.] [paso] [velocidad]` | frente a una pared: filas de fotos en zigzag (fila, subida, fila de vuelta) con solapamiento | 300, 100..300, 50 |

Ejemplos:

```
tello> flower dry                    # ver el plan
tello> flower 150 200 45 out         # círculo de 2 m de radio a 1,5 m, fotos hacia fuera cada 45°
tello> wheel 250 100 ccw video       # rueda en sentido antihorario, con vídeo de todo el vuelo
tello> timelapse 5 60                # 60 fotogramas cada 5 s, luego timelapse.mp4
```

## 7. Vídeo y foto

- `video show` — ventana con la imagen en directo e información en pantalla (batería, altura, rumbo); `video hide` la cierra.
- `video start` / `video stop` — grabación en `~/Videos/tello/tello_FECHA_HORA.mp4` (sin recodificar).
- `photo` — un fotograma de la cámara en `~/Pictures/tello/`. El Tello no tiene memoria para fotos:
  la imagen se toma del flujo de vídeo en el ordenador.

## 8. Teclado y mando

- **`keyboard`** (en el aire): control con teclas en la ventana de vídeo — `W/A/S/D` movimiento,
  flechas arriba/abajo — altura, izquierda/derecha — giro, `Esc` — volver a los comandos.
  `kbspeed 60` — fuerza de los «sticks» (40 por defecto).
- **`joystick`** (también en tierra): stick derecho — movimiento, stick izquierdo — altura y giro,
  `A` — despegue, `B` — aterrizaje, `Back`+`Start` a la vez — parada de emergencia de los motores,
  `Esc` en la ventana o `Guide` — volver a los comandos.

## 9. Misiones y grabación de rutas

Una misión es un archivo de texto con una línea de comandos por línea; `#` es un comentario. En
el primer error la misión se detiene y el dron aterriza.

```
# mission.txt
takeoff
up 50
square 100
land
```

```bash
./build/tello --mission mission.txt    # código de salida 0 solo si todas las líneas tuvieron éxito
```

Dentro del programa: `mission mission.txt`.

**Grabación de rutas:** `record start route.txt` — todos los comandos de vuelo, movimiento,
figuras y cámara se escriben en el archivo hasta `record stop`. Un vuelo con teclado o mando se
graba como un comando `go` más un giro, según la posición calculada. `mission route.txt` repite la
ruta. `record status` — cuántas líneas se han grabado ya.

## 10. Comandos propios (alias)

```
tello> alias hop up $1; down $1      # $1..$9 — argumentos, $* — todos
tello> takeoff; hop 50; land
tello> aliases                       # lista; unalias hop — borrar
```

Los alias se guardan en `~/.config/tello/aliases` y funcionan también en las misiones.

## 11. Configuración

Archivo `~/.config/tello/config`; las opciones de línea de comandos tienen prioridad.

```
host = 192.168.10.1
port = 8889
state_port = 8890        # 0 — sin telemetría
video_port = 11111
model = tello            # o tello-edu
warn_battery = 15        # por ciento
land_battery = 10
kbspeed = 40             # teclado, 10..100
js_deadzone = 10         # mando, zona muerta del stick en por ciento, 0..50
log_dir = ~/.local/state/tello/logs   # registros de sesión; "off" = sin registro
log_keep = 30            # cuántos registros recientes conservar
key_forward = w          # y key_back/left/right/up/down/turn_left/turn_right
```

## 12. Registro de vuelo

Cada ejecución escribe un registro de texto para el análisis tras el vuelo, un archivo por sesión:
`~/.local/state/tello/logs/tello_AAAAMMDD_HHMMSS.log` (o `$XDG_STATE_HOME/tello/logs`),
se conservan los 30 más recientes. `log` muestra el archivo actual; `--log-dir CARPETA` o
`log_dir` cambian el lugar, `--no-log` o `log_dir = off` lo desactivan. Las entradas se escriben
al momento: un fallo del programa no pierde nada.

Qué contiene:
- **cabecera de la sesión**: versión, línea de comandos, modelo, dirección y puertos, límites de batería;
- **cada línea introducida y cada comando** con `begin` / `end ...: ok|failed: <motivo>|exception, N ms` —
  un `begin` sin `end` significa que el comando se colgó o que el programa murió en él;
- **cada comando SDK y cada respuesta** (`> takeoff`, `< ok  [takeoff, 2140 ms]`), tiempos de
  espera agotados, respuestas tardías; los `rc` repetidos se cuentan, no se listan;
- **posición** tras cada comando de vuelo, **telemetría** cada segundo en el aire (cada 10 s en
  tierra), `telemetry lost` / `telemetry back`;
- **todo lo que salió en pantalla** (`console: ...`), avisos de batería, eventos del flujo de
  vídeo, fotos y grabaciones, Ctrl+C, errores y excepciones inesperadas, señales fatales
  (`*** FATAL signal N`).

Una línea es así: `2026-11-05 09:24:20.387 INFO  main     drone: > takeoff`
(hora, nivel `DEBUG/INFO/WARN/ERROR`, hilo, componente). Buscar problemas:
`grep -E 'WARN|ERROR|FATAL' ~/.local/state/tello/logs/tello_*.log`.

## 13. Lo que hay que saber del Tello

- **La cámara mira solo hacia delante y no se inclina.** El punto de despegue solo aparece en la
  imagen si el radio es mayor que unas dos veces la altura. `loop` pasa `radio` cm por detrás del
  punto de despegue — póngase a un lado o más atrás.
- **No hay GPS.** La posición se calcula por estima — a partir de los comandos ejecutados y de la
  velocidad de la telemetría. El error crece con cada metro; `return` no es un regreso por GPS,
  sino tramos rectos según esa estimación.
- El dron no ejecuta desplazamientos de menos de 20 cm; un comando es como máximo 500 cm.
- Volteretas solo con más del 50 % de batería.
