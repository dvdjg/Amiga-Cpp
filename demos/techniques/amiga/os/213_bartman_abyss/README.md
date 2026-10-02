# Demo 213 — Bartman "abyss" en la fachada de juego (`eng::App`/`Screen`)

Port al engine de la demo clásica de **Bartman/vscode-amiga-debug** (`BartmanBasic/main.c`):
escena de **320×256 con 5 planos interleaved** que muestra la imagen *abyss*, **16 BOBs
enmascarados** (cookie-cut `$CA`) recorriendo la banda inferior por senos y **música P61**
(ThePlayer) conducida por el engine.

El juego describe **qué** quiere con el vocabulario del engine y no sondea hardware: `eng::App`
gestiona el bucle, el display, el latido de VBlank y la cola de mensajes; `Screen` expone
`sprite()`/`clear_box()` y `app.audio().play_music()` arranca la música. Ver
`docs/engine/architecture/GAME_API_TWO_LEVELS.md`.

## Qué muestra

- La imagen *abyss* (5 planos interleaved, 32 colores) tal cual.
- 16 BOBs (glifo `あ`) repartidos por la banda inferior (filas 200..255): **desfase horizontal**
  en módulo 51 y **seno vertical** (tablas exactas de `BartmanBasic/main.c`), con el frame de la
  hoja ciclando 0..5. El `clear_box` de la banda usa **un** blit D-only interleaved.
- **Cookie-cut `$CA` en un solo blit por BOB**: la hoja `[imagen][máscara]` se reproduce con
  `A=máscara`, `B=imagen`, `ASH=BSH=x&15`, `height=16*5`, `AMOD=BMOD=4`, `DMOD=36`.
- La música P61 sonando; el motor la avanza en su propio latido de VBlank.
- El **puerto de mensajes** del mini-SO: el latido de VBlank publica `MsgType::VBlank` en
  `app.port()` y el juego lo drena en `update`.

## Invariantes / detalles que importan

- **Layout interleaved**: fila de 5 planos × 40 B = 200 B; `BPLxPT = base + p*40`. El bitmap se
  dibuja *in place* (sin doble buffer).
- **BOB de una pasada** (`bob_draw_interleaved_pair`): por cada fila de cada plano la hoja lleva
  `[imagen `w/16` palabras][máscara `w/16` palabras]` (8 B). El encoder
  (`blitter_job_from`) conecta `A=máscara` (segunda mitad) y `B=imagen` (primera mitad) con
  `ASH`/`BSH` iguales; el bit de máscara de cada plano va en la propia hoja. Ficha:
  `docs/reference/amiga/techniques/interleaved-bob-single-blit.md`.
- **Assets en Chip**: el engine copia los blobs desde `.rodata` a bloques Chip (`eng::Assets`)
  porque el Blitter y Paula solo ven Chip RAM.
- **Latido**: `App::run()` registra el hook de VBlank que llama a `eng::os::tick()` y publica
  `VBlank`; `update`/`render` corren en el bucle principal.

## Criterio de aceptación

- `state=3` (Ready) con `detail=0x21300`.
- En la captura: la imagen *abyss* y los `あ` de colores repartidos por la banda inferior.

## Compilar / ejecutar / analizar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/os/213_bartman_abyss --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --warp
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --warp --screenshot out/tmp/213.png
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/os/213_bartman_abyss
```

## Evidencia de referencia (A500_debug)

- `detail=0x21300` (`state=3`); captura `out/tmp/213.png` con la imagen y los BOBs.
- Medición `tools/debug/measure-fps.mjs 213_bartman_abyss A500_debug`: **23,3 fps**,
  **304287 ciclos/frame** (≈2,14 campos/frame). El cuello es la **ejecución del Blitter**
  (17 blits/frame: 1 clear + 16 cookie-cut). No alcanza el objetivo de 1 frame por VBlank:
  queda **abierta** a optimización (ver «Próximos pasos»).
- **Audio**: el runner no captura PCM, así que la salida de Paula no se verifica aquí; el
  reproductor P61 ya está validado por demos 060 (`music_pt`) y 272 (`audio_stream`).

## Límites / piezas pendientes

- **No hay fine-scroll de `BPLCON1`**: el original mueve el playfield por seno; aquí el efecto
  pendiente es solo de los BOBs. El `Scene` planar publica punteros por `commit`, no parchea
  `BPLCON1` por frame; haría falta un `PatchHandle` en el plan (materia de
  `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md`).
- **No hay salida por botón de ratón**: `App` no expone `quit()` y la entrada del mini-SO llega
  a `eng::os::system_port()`, no a `app.port()`; el runner cierra la instancia.

## Assets

En `assets/amiga/sprites/abyss/` (origen `BartmanBasic`, uso interno de prueba):

| Archivo | Formato |
|---|---|
| `abyss.bpl` | Interleaved 320×256×5, 40 B por fila/plano (51200 B), sin cabecera. |
| `abyss.pal` | 32 colores Amiga `$0RGB` (64 B). |
| `bob.bpl` | 6 frames de 32×16; por fila de plano `[imagen 2 palabras][máscara 2 palabras]` (3840 B). |

Módulo: `assets/amiga/audio/testmod.p61`.

## Referencias

- Original: `BartmanBasic/main.c` del fork Bartman/vscode-amiga-debug.
- Técnica del BOB: `docs/reference/amiga/techniques/interleaved-bob-single-blit.md`.
- Fachada de juego: `docs/engine/architecture/GAME_API_TWO_LEVELS.md`.
- Mini-SO: `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`.
- Música: `docs/engine/architecture/MUSIC_PLAYER.md`.

Tests: HOST-072 (geometría de BOB), HOST-365 (encoder del Blitter), HOST-219 (mini-SO).
