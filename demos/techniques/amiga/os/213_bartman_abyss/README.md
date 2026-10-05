# Demo 213 — Bartman "abyss" en la fachada de juego (`eng::App`/`Screen`)

Port al engine de la demo clásica de **Bartman/vscode-amiga-debug** (`BartmanBasic/main.c`):
escena de **320×256 con 5 planos interleaved** que muestra la imagen *abyss* sobre fondo claro,
**16 BOBs enmascarados** (cookie-cut `$CA`) recorriendo la banda inferior por senos, el
**fine-scroll** del playfield (`BPLCON1`, tabla `sinus15`) que "menea" el logo, y **música P61**
(ThePlayer) conducida por el engine.

El juego describe **qué** quiere con el vocabulario del engine y no sondea hardware: `eng::App`
gestiona el bucle, el display, el latido de VBlank y la cola de mensajes; `Screen` expone
`sprite()`/`clear_box()`, `app.set_fine_scroll()` mueve el fondo y `app.audio().play_music()`
arranca la música. Ver `docs/engine/architecture/GAME_API_TWO_LEVELS.md`.

## Qué muestra

- La imagen *abyss* (5 planos interleaved, 32 colores) tal cual.
- **16 BOBs** (glifo `あ`) repartidos por la banda inferior (filas 200..255): **desfase
  horizontal** en módulo 51 y **seno vertical**, con el frame de la hoja ciclando 0..5 (cada
  frame colorea el glifo con planos distintos). El `clear_box` de la banda usa **un** blit
  D-only interleaved.
- **Fine-scroll del playfield** (`BPLCON1`, tabla `sinus15`): `app.set_fine_scroll(px)` mueve
  todo el fondo (logo, gorro) ±15 px por seno; se aplica en el VBlank para no partir scanlines.
- **Gradiente de la copper2** (líneas `$41..$4F`): la original salta a una segunda copperlist
  (`COPJMP2`) que pinta `COLOR00` con `0x0111..0x0fff` línea a línea; aquí el `compose` expresa
  lo mismo con una etapa `intents` de 15 `CopperIntent::PaletteLine`. Como la original acaba en
  `0x0fff` (= el `COLOR00` de la paleta base) y no lo restaura, tras `$4F` el fondo sigue blanco.
- **Cookie-cut `$CA` en un solo blit por BOB**: la hoja `[imagen][máscara]` se reproduce con
  `A=máscara`, `B=imagen`, `ASH=BSH=x&15`, `height=16*5`, `AMOD=BMOD=4`, `DMOD=36`.
- La música P61 sonando; el motor la avanza en su propio latido de VBlank.
- El **puerto de mensajes** del mini-SO: el latido de VBlank publica `MsgType::VBlank` en
  `app.port()` y el juego lo drena en `update`.
- **Overlay de depuración** (`app.debug()`): igual que la original, un rectángulo relleno, un
  rectángulo de borde y un texto desplazándose con `f = frameCounter & 255` (coordenadas PAL ×2).
  No aparece en las capturas de gameplay.

## Invariantes / detalles que importan

- **Layout interleaved**: fila de 5 planos × 40 B = 200 B; `BPLxPT = base + p*40`. El bitmap se
  dibuja *in place* (sin doble buffer).
- **BOB de una pasada** (`bob_draw_interleaved_pair`): por cada fila de cada plano la hoja lleva
  `[imagen `w/16` palabras][máscara `w/16` palabras]` (8 B). El encoder
  (`blitter_job_from`) conecta `A=máscara` (segunda mitad) y `B=imagen` (primera mitad) con
  `ASH`/`BSH` iguales. Ficha: `docs/reference/amiga/techniques/interleaved-bob-single-blit.md`.
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

- `detail=0x21300` (`state=3`); captura con la imagen y los 16 BOBs.
- **BOBs sin flicker**: la banda inferior es nítida en cada frame (la capa determinista de
  `tools/vision-review/flicker-check.mjs` sitúa los candidatos en la región de la imagen por el
  desplazamiento global, no en la banda del BOB).
- Medición con `tools/debug/measure-fps.mjs 213_bartman_abyss A500_debug`.

## Límites / piezas pendientes

- **Fine-scroll sin columna de guarda**: el `Scene` usa `DDFSTRT=$38` (fetch estándar), igual
  que el original; al desplazar, el borde izquierdo envuelve la palabra derecha de la fila. En
  *abyss* el borde es fondo blanco y no se ve; para contenido a sangre haría falta un modo
  *guard-aware* (`DDFSTRT=$30` + palabra de guarda por fila), convención de
  `eng::effects::FineScroll`.
- **No hay salida por botón de ratón**: `App` no expone `quit()` y la entrada del mini-SO llega
  a `eng::os::system_port()`, no a `app.port()`; el runner cierra la instancia.

## Overlay y recursos de depuración

- La demo pinta su overlay con `app.debug()` (rects/texto, igual que la original) **y** registra
  el bitmap `abyss` y la hoja `bob` y la paleta en el debugger de WinUAE (`register_bitmap`/
  `register_palette`, equivalentes a los `debug_register_bitmap/palette` de la original). La
  copperlist no se registra: el motor la compone y la demo no la posee.

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
- Telemetría/overlay: `eng/debug/telemetry.hpp`, `docs/tools/PROFILING_FROM_AGENT.md`.
- Música: `docs/engine/architecture/MUSIC_PLAYER.md`.

Tests: HOST-072 (geometría de BOB), HOST-365 (encoder del Blitter), HOST-219 (mini-SO).
