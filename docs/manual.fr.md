# DJI Tello CLI — manuel d'utilisation

[English](../README.md) · [Русский](manual.ru.md) · [Srpski](manual.sr.md) · [Deutsch](manual.de.md) · **Français** · [Español](manual.es.md)

`tello` est un programme interactif en ligne de commande pour piloter le drone DJI / Ryze Tello
depuis un ordinateur sous Linux : commandes, figures avec photos et vidéo, missions depuis un
fichier, pilotage au clavier et à la manette. Il est livré avec le simulateur `tello-sim` : tout
peut être essayé sans drone.

## 1. Installation

```bash
sudo apt install cmake g++ googletest libavcodec-dev libavformat-dev libavutil-dev libsdl2-dev
make build      # build/tello et build/tello-sim
make test       # vérification : tests contre le simulateur
```

Paquets prêts : `make package` → `build-package/tello_<version>_amd64.deb` et `.rpm`.

## 2. Essayer sans drone

```bash
make run-sim    # le simulateur sur 127.0.0.1:8889 et le programme connecté à lui
```

Ou dans deux terminaux :

```bash
make sim                                     # terminal 1 : le faux drone, affiche chaque commande
./build/tello --host 127.0.0.1 --port 8889   # terminal 2
```

Le simulateur répond comme le vrai drone, suit la hauteur, la position et la décharge de la
batterie, atterrit seul après 15 s de silence et, après `streamon`, envoie une image de test de la
caméra. Avant le premier vrai vol, essayez-y tout ce que vous prévoyez de faire.

## 3. Premier vol

1. Allumez le drone et connectez-vous à son Wi-Fi (`TELLO-XXXXXX`).
2. Lancez `./build/tello` (pour le Tello EDU : `./build/tello --model tello-edu`).
3. Tapez `help`.

```
tello> takeoff; up 50; cw 90
tello> battery
tello> land
```

Plusieurs commandes tiennent sur une ligne, séparées par `;` ; la ligne s'arrête à la première
erreur. `Tab` complète les commandes, les flèches parcourent l'historique.

Options de lancement : `--host`, `--port`, `--state-port` (0 — sans télémétrie), `--video-port`,
`--model`, `--mission FICHIER`, `--log-dir DOSSIER`, `--no-log`.

## 4. Sécurité

- **Ctrl+C en vol — atterrissage.** Le programme ne se ferme jamais avec le drone en l'air.
  Au sol, Ctrl+C efface seulement la ligne ; pour quitter : `exit` ou Ctrl+D.
- **Keepalive :** le drone atterrit seul après 15 s sans commande, c'est pourquoi le programme lui
  envoie un signal tant que vous ne tapez rien.
- **Batterie :** avertissement à 15 %, atterrissage automatique à 10 % (réglable).
- **Sortie sûre :** `exit`, Ctrl+D ou la fin d'une mission avec le drone en l'air atterrissent d'abord
  et ne quittent qu'après l'atterrissage. En cas d'échec, le programme reste ouvert (`land` ou
  `emergency` ; un second `exit` quitte quand même).
- **Atterrissage d'urgence :** si le programme meurt en vol (plantage, `kill`, terminal fermé), il
  envoie `land` avant et le note dans le journal. Si une commande bloque 45 s, le drone atterrit aussi.
- Non couvert : `kill -9`, portable à plat, perte du Wi-Fi — le Tello atterrit alors seul après 15 s
  sans commande.
- **`emergency`** coupe les moteurs immédiatement, sans confirmation — le drone tombe. Uniquement en dernier recours.
- Volez au-dessus d'un sol meuble, loin des personnes et des arbres ; essayez d'abord les figures avec `dry` (voir plus bas).

## 5. Commandes

`help` n'affiche que ce que le modèle choisi sait faire ; `help <commande>` donne les détails.

| Groupe | Commandes |
|---|---|
| Système | `help [cmd]`, `version`, `info`, `wait <s>`, `alias`, `aliases`, `unalias <nom>`, `exit`, `log` (chemin du journal de session) |
| Vol | `connect`, `takeoff`, `land`, `hover` (EDU), `emergency` |
| Déplacement | `forward/back/left/right/up/down <20..500 cm>`, `cw/ccw <1..360°>`, `flip <l\|r\|f\|b>`, `speed <10..100 cm/s>`, `go <x> <y> <z> <vitesse>`, `return` / `home` (revenir au-dessus du point de décollage et atterrir) |
| Caméra vers un point | `camera_to_center <x> <y>`, `camera_from_center <x> <y>`, `orbit <50..500 cm> [cw\|ccw] [vitesse]` (tour d'un point devant, en 12 segments) |
| Missions | `mission <fichier>`, `square <cm>`, `record start <fichier>\|stop\|status` |
| Clavier | `keyboard` / `kb`, `kbspeed [10..100]` |
| Manette | `joystick` / `js` |
| Vidéo et photo | `video start\|stop\|show\|hide\|status`, `photo` |
| Télémétrie | `battery`, `height`, `wifi`, `get_speed`, `temp`, `time`, `attitude`, `baro`, `tof`, `sdk` (EDU), `sn` (EDU), `state`, `position` / `pos` |

`takeoff` n'a pas de hauteur : décollage (environ 80 cm), puis `up <cm>`.

## 6. Figures

Une figure est un vol complet en une commande : décollage, figure, retour au centre,
atterrissage. Tous les paramètres sont facultatifs et se suivent dans l'ordre ; les mots (`cw`,
`in`, `flat`...) peuvent se placer n'importe où. Unités : centimètres, degrés, cm/s.

- **`dry`** à la fin de n'importe quelle figure — rien n'est envoyé au drone ; on voit la liste des
  commandes, le nombre de photos, la durée, la hauteur maximale et la consommation de batterie.
  Exemple : `flower 300 150 dry`.
- **`video`** — filme tout le vol dans `~/Videos/tello/`.
- Les photos d'une figure vont dans un dossier à part `~/Pictures/tello/<figure>_DATE_HEURE/01.jpg, 02.jpg...`
- Avant le décollage, le programme vérifie qu'il y a une image de la caméra ; sans elle, il ne décolle pas.
- Si une commande échoue en vol, la figure s'arrête et le drone atterrit.

| Figure | Ce qu'elle fait | Par défaut |
|---|---|---|
| `flower [hauteur] [rayon] [pas] [in\|out] [cw\|ccw] [vitesse]` | cercle horizontal autour du point de décollage, une photo tous les `pas` degrés ; caméra `in` — vers le centre, `out` — vers l'extérieur | 200, 100, 30°, in, cw, 30 |
| `wheel [hauteur] [rayon] [pas] [cw\|ccw] [in\|out] [vitesse]` | cercle vertical en travers de votre regard (gauche-droite), commence en haut ; `cw` — d'abord à droite ; la hauteur doit dépasser le rayon | 200, 100, 30°, cw, in |
| `loop [hauteur] [rayon] [pas] [away\|toward] [in\|out\|side] [vitesse]` | cercle vertical qui s'éloigne de vous et revient (avant-arrière) ; caméra vers vous, devant ou sur le côté | 200, 100, 30°, away, in |
| `panorama [hauteur] [pas] [cw\|ccw]` | rotation sur place avec une photo à chaque pas — panorama circulaire | 200, 30° |
| `tower [de] [à] [pas]` | montée verticale avec une photo à chaque hauteur (façade, arbre) | 100..500 par 50 |
| `dronie [distance] [hauteur] [vitesse]` | vidéo : caméra sur vous, le drone s'éloigne en diagonale vers l'arrière et le haut, puis revient | 300, 200 |
| `reveal [distance] [hauteur] [vitesse]` | vidéo : part bas (50 cm), vole vers l'avant et le haut — la vue « se dévoile » | 300, 200 |
| `bounce [fois] [amplitude] [cw\|ccw] [vitesse]` | montées et descentes ; avec `cw`/`ccw`, un tour complet en même temps | 3 fois, 100 |
| `flips [l\|r\|f\|b ...]` | série de flips à 150 cm ; au moins 50 % de batterie | les quatre |
| `helix [tours] [de haut.] [à haut.] [de rayon] [à rayon] [pas] [cw\|ccw] [vitesse]` | vidéo : spirale autour du point de décollage vers le haut et l'extérieur, caméra vers le centre | 2 tours, 100..300, 100..200 |
| `boomerang [longueur] [largeur] [montée] [cw\|ccw] [vitesse]` | vidéo : ovale autour du point de décollage, plus haut à l'extrémité lointaine, caméra vers le centre | 400, 200, 100 |
| `eight [rayon] [hauteur] [pas] [flat\|upright] [vitesse]` | vidéo : un huit, horizontal (`flat`) ou vertical (`upright`) | 100, 200, flat |
| `polygon <coins> [rayon] [hauteur] [flat\|upright] [vitesse]` | polygone de 3 à 12 coins dans les airs | 100, 200, flat |
| `star [rayon] [hauteur] [flat\|upright] [vitesse]` | étoile à cinq branches | 100, 200, flat |
| `rings [rayon] [de haut.] [à haut.] [anneaux] [pas] [vitesse]` | plusieurs cercles de photos à différentes hauteurs, caméra vers le centre, tout dans un dossier — pour un modèle 3D (Meshroom, etc.) | 150, 100..300, 3 anneaux |
| `timelapse [intervalle s] [images] [hauteur]` | reste en vol stationnaire et prend une image toutes les N secondes ; après l'atterrissage, assemble les images en `timelapse.mp4` (10 images/s) | 2 s, 30 images, 200 |
| `hyperlapse [distance] [images] [hauteur] [vitesse]` | vole lentement vers l'avant en prenant des images, revient ; les assemble en `hyperlapse.mp4` | 500, 20 images, 200, 20 |
| `scan [largeur] [de haut.] [à haut.] [pas] [vitesse]` | face à un mur : rangées de photos en serpentin (rangée, montée, rangée retour) avec recouvrement | 300, 100..300, 50 |

Exemples :

```
tello> flower dry                    # voir le plan
tello> flower 150 200 45 out         # cercle de 2 m de rayon à 1,5 m, photos vers l'extérieur tous les 45°
tello> wheel 250 100 ccw video       # roue dans le sens antihoraire, avec la vidéo de tout le vol
tello> timelapse 5 60                # 60 images toutes les 5 s, puis timelapse.mp4
```

## 7. Vidéo et photo

- `video show` — fenêtre avec l'image en direct et des infos à l'écran (batterie, hauteur, cap) ; `video hide` la ferme.
- `video start` / `video stop` — enregistrement dans `~/Videos/tello/tello_DATE_HEURE.mp4` (sans réencodage).
- `photo` — une image de la caméra dans `~/Pictures/tello/`. Le Tello n'a pas de mémoire photo :
  l'image est prise du flux vidéo sur l'ordinateur.

## 8. Clavier et manette

- **`keyboard`** (en vol) : pilotage avec les touches dans la fenêtre vidéo — `W/A/S/D` déplacement,
  flèches haut/bas — hauteur, gauche/droite — rotation, `Échap` — retour aux commandes.
  `kbspeed 60` — force des « manches » (40 par défaut).
- **`joystick`** (aussi au sol) : stick droit — déplacement, stick gauche — hauteur et rotation,
  `A` — décollage, `B` — atterrissage, `Back`+`Start` ensemble — arrêt d'urgence des moteurs,
  `Échap` dans la fenêtre ou `Guide` — retour aux commandes.

## 9. Missions et enregistrement de trajet

Une mission est un fichier texte avec une ligne de commandes par ligne, `#` commence un
commentaire. À la première erreur, la mission s'arrête et le drone atterrit.

```
# mission.txt
takeoff
up 50
square 100
land
```

```bash
./build/tello --mission mission.txt    # code de sortie 0 seulement si toutes les lignes ont réussi
```

Dans le programme : `mission mission.txt`.

**Enregistrement du trajet :** `record start route.txt` — toutes les commandes de vol, de
déplacement, de figures et de caméra sont écrites dans le fichier jusqu'à `record stop`. Un vol au
clavier ou à la manette est enregistré comme une commande `go` plus une rotation, d'après la
position calculée. `mission route.txt` refait le trajet. `record status` — combien de lignes sont déjà enregistrées.

## 10. Commandes personnelles (alias)

```
tello> alias hop up $1; down $1      # $1..$9 — arguments, $* — tous
tello> takeoff; hop 50; land
tello> aliases                       # liste ; unalias hop — supprimer
```

Les alias sont enregistrés dans `~/.config/tello/aliases` et fonctionnent aussi dans les missions.

## 11. Réglages

Fichier `~/.config/tello/config` ; les options de la ligne de commande sont prioritaires.

```
host = 192.168.10.1
port = 8889
state_port = 8890        # 0 — sans télémétrie
video_port = 11111
model = tello            # ou tello-edu
warn_battery = 15        # pour cent
land_battery = 10
kbspeed = 40             # clavier, 10..100
js_deadzone = 10         # manette, zone morte du stick en pour cent, 0..50
log_dir = ~/.local/state/tello/logs   # journaux de session ; "off" = pas de journal
log_keep = 30            # nombre de journaux récents conservés
key_forward = w          # et key_back/left/right/up/down/turn_left/turn_right
```

## 12. Journal de vol

Chaque lancement écrit un journal texte pour l'analyse après le vol, un fichier par session :
`~/.local/state/tello/logs/tello_AAAAMMJJ_HHMMSS.log` (ou `$XDG_STATE_HOME/tello/logs`),
les 30 plus récents sont conservés. `log` affiche le fichier courant ; `--log-dir DOSSIER` ou
`log_dir` changent l'emplacement, `--no-log` ou `log_dir = off` désactivent le journal. Les
entrées sont écrites immédiatement — un plantage ne perd rien.

Contenu :
- **en-tête de session** : version, ligne de commande, modèle, adresse et ports, seuils de batterie ;
- **chaque ligne saisie et chaque commande** avec `begin` / `end ...: ok|failed: <raison>|exception, N ms` —
  un `begin` sans `end` signifie que la commande a bloqué ou que le programme est mort dedans ;
- **chaque commande SDK et chaque réponse** (`> takeoff`, `< ok  [takeoff, 2140 ms]`), délais
  dépassés, réponses tardives ; les commandes `rc` répétées sont comptées, pas listées ;
- **position** après chaque commande de vol, **télémétrie** chaque seconde en vol (toutes les 10 s
  au sol), `telemetry lost` / `telemetry back` ;
- **tout ce qui s'est affiché à l'écran** (`console: ...`), alertes batterie, événements du flux
  vidéo, photos et enregistrements, Ctrl+C, erreurs et exceptions inattendues, signaux fatals
  (`*** FATAL signal N`).

Une ligne ressemble à : `2026-11-05 09:24:20.387 INFO  main     drone: > takeoff`
(heure, niveau `DEBUG/INFO/WARN/ERROR`, thread, composant). Chercher les problèmes :
`grep -E 'WARN|ERROR|FATAL' ~/.local/state/tello/logs/tello_*.log`.

## 13. Ce qu'il faut savoir sur le Tello

- **La caméra regarde seulement devant et ne s'incline pas.** Le point de décollage n'est dans
  l'image que si le rayon dépasse environ deux fois la hauteur. `loop` passe `rayon` cm derrière
  le point de décollage — placez-vous sur le côté ou plus loin derrière.
- **Pas de GPS.** La position est estimée à l'estime — à partir des commandes exécutées et de la
  vitesse de la télémétrie. L'erreur grandit à chaque mètre ; `return` n'est pas un retour GPS,
  mais des segments droits d'après cette estimation.
- Le drone n'exécute pas les déplacements de moins de 20 cm ; une commande fait au plus 500 cm.
- Flips seulement avec plus de 50 % de batterie.
