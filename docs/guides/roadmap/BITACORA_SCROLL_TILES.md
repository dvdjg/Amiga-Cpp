# Bitácora: scroll por tiles (2026-08)

Registro de estado, correcciones y lecciones del scroll por tiles del engine. Es material de bitácora, no de referencia: describe la evolución y los descubrimientos. El estado vigente y las próximas direcciones están en [ROADMAP_UNIFICADO.md](ROADMAP_UNIFICADO.md); los contratos de diseño, en `docs/engine/architecture/`.

## 1. Scroll fino de la demo 101

RESUELTO el salto del cruce de tile. La causa raíz era el signo de `BPLCON1` (invertido) y el puntero coarse: el driver usaba `BPLCON1=fine` con `fetch=coarse-16`, lo que invertía el sentido del scroll dentro de cada tile y producía un salto de ~31 px en el cruce de 16 px. Se sustituyó por la fórmula canónica de ACE/HRM: `BPLCON1=(16-fine)&15` y `fetch=(scroll_x-1)&~15` (un word antes solo en `fine==0`), que da `display_start == scroll_x` continuo en todo el rango.

En su momento quedó pendiente el doble buffer de la copperlist. Validación: `analyze-fine-scroll.sh --warp` y `analyze-sequence.sh --warp`.

## 2. Scroll genérico multi-modo (2026-08)

El driver de scroll por tiles vive en `engine/include/eng/graphics/drivers/tile_scroll.hpp` como `TileScrollScene<Mode>` (template sobre el modo), con scroll por playfield (`TileScrollInput`) y override coarse por bitplane (`plane[i]`, preparado para RoboCod). `ehb_tile_scroll.hpp` es un shim de compatibilidad (`EhbTileScrollScene` = single 6).

Las demos 103/104 (`demos/amiga/103_tile_scroll_ring`, `104_tile_scroll_ring_dualpf`) demuestran el scroll por tiles single y dual. El test de descomposición de scroll para 4/5/6 single y 2+3/3+3 dual es `node tools/analyze/verify-tile-scroll-modes.mjs`.

## 3. Rendimiento del scroll por tiles (lecciones aprendidas)

- El scroll del chipset (BPLxPT + BPLCON1 vía Copper) es barato; la CPU solo calcula offsets y programa registros, como en los juegos reales (Mega Typhoon). El coste real del frame estaba en la capa software de prefetch, no en el display.
- `ProgressiveTileScheduler::take_budget` era O(n²): desplazaba toda la cola por cada job tomado. Con la cámara rápida (2 px/frame) y franjas encoladas en cada cruce, dominaba el update y la demo caía de 50 fps a ~36. Se arregló con puntero de cabeza (O(budget), compactación amortizada) en `tilemap/tile_scroll.hpp`.
- En dual playfield, `make_playfield_upload_jobs` emitía UN job por plano por tile (3× en 3+3). Cada job paga wait_blitter + programación de registros, y el overhead por blit es lo que domina en los cruces del ring dual. Se fusionó en un solo job por tile con `destination_plane_stride = 2*plane_bytes` (los planos de un playfield están intercalados: PF1=1,3,5 / PF2=2,4,6). La demo 104 subió de ~41,5 a ~47,6 fps; el resto del gap es el overhead del emulador por blit en los frames de cruce, no el hardware (en Amiga real cabe de sobra en 20 ms).
- Los checkpoints del periférico (`debugperiph checkpoints`) añaden ~10 fps de overhead al update; medir fps con ellos puestos engaña. Quitarlos para medir.
### Cifras de fps (trazables)

La cifra histórica (101=~48, 102=~50, 103=~50, 104=~47,6) no era reproducible: no se anotó commit/config/fecha, la demo 102 ya no existe en el árbol de fuentes y las medidas posteriores daban otra cosa para 103/104. La tabla trazable vigente es:

| Demo | `CONFIG_ID` | fps emulado | ciclos/frame | `detail` | fecha | commit |
|---|---|---|---|---|---|---|
| `101_ehb_tile_scroll_driver` | `A500_debug` | 49,92 | 142 102 | 0x11595823 | 2026-09-17 | `53af1d4` |
| `103_tile_scroll_ring` | `A500_debug` | 32,95 | 215 306 | 0x13100200 | 2026-09-17 | `1490dfc` |
| `104_tile_scroll_ring_dualpf` | `A500_debug` | 30,02 | 236 313 | 0x14020600 | 2026-09-17 | `1490dfc` |
| `086_bob_objects` | `A500_debug` | 49,92 | 142 102 | 0x1000303 | 2026-09-18 | `11014f8` |

Contexto de medida: `CONFIG_ID` **`A500_debug`** (build `--debug`, `-O1`), emulador **WinUAE-DBG x86**, herramienta `tools/debug/measure-fps.mjs` (contador de ciclos del periférico `0xB7E928`, 7,09379 MHz). El fps depende de la **fase** del recorrido (`detail`): comparar siempre con el mismo `detail`. En hardware real las demos de scroll van a 50 fps.

**Protocolo para reproducir y añadir filas:**

1. Compilar: `bash ./tools/build/build-demo.sh demos/amiga/<demo> --debug` (o `--release`).
2. Medir y anotar de una vez: `node tools/debug/record-fps.mjs <demo> A500_debug` (ejecuta `measure-fps.mjs --json` y anexa/actualiza la fila con fecha, commit, config y `detail`). Con `--dry-run` solo imprime la fila.
3. A mano (sin el helper): `node tools/debug/measure-fps.mjs <demo> A500_debug --json` y pegar la fila con fecha/commit/`detail`.

> El fps depende de la fase (`detail`). Para no depender de acertar la misma fase, tanto `record-fps` como `check-fps` toman varias muestras (`--samples`, 2 por defecto) y usan la **mejor**; se compara contra el baseline (también mejor-de-N). El **gate periódico** es `bash ./tools/run-fps-gate.sh` (no en cada regresión); la regresión lo lleva opt-in con `--fps-gate`.

**Notas de la re-medición (2026-09-17):**

- La demo `102_tile_scroll_dualpf` ya no existe en `demos/amiga/` (solo queda su artifact en `out/demos/`), así que su fila no es reproducible.
- `measure-fps.mjs` antes medía contra el `dh1/a.exe` que hubiera de un `run-demo` previo: si ese `a.exe` no coincidía con el `.map` (p. ej. una build vieja), la dirección de `g_eng_run_status` caía en otro sitio y la medida salía inválida (`detail=0x0`). La tool ahora **copia la build recién compilada a `dh1/a.exe` antes de medir**, así que no depende de un `run-demo` reciente (solo de que exista `runner.uae`).
