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
- Fps medidos (emulador WinUAE-DBG, `-O1`): 101=~48, 102=~50, 103=~50, 104=~47,6. En hardware real los cuatro van a 50 fps.

> Nota de trazabilidad: las cifras de fps dependen del emulador, del flag de optimización y de la fase medida. La discrepancia de la 103 en medidas posteriores se investiga en [NORMALIZACION_REPO.md](NORMALIZACION_REPO.md); hasta cerrarlo, trátese cada cifra con su contexto de medida (fecha + config).
