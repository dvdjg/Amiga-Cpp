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

### Cifras de fps: trazabilidad

Las cifras anteriores son **referencias históricas, no gates**, y hoy **no son reproducibles de forma directa**:

- **La demo `102` ya no existe** en `demos/amiga/`; solo queda su artifact (`out/demos/102_tile_scroll_dualpf/`), lo que hace imposible reproducir esa fila.
- No se registró el **commit, la fecha, el `CONFIG_ID` ni el flag de optimización** de la medida, y el fps depende de la **fase** del recorrido (`detail` cambia), así que una cifra suelta no es comparable entre revisiones.
- Medidas posteriores dan para la 103 ~33 fps (no ~50); el A/B con y sin `DoubleBuffer` (33,50 vs 32,67) descarta que sea del refactor de buffers. El análisis está en [NORMALIZACION_REPO.md](NORMALIZACION_REPO.md) §0.7.

**Protocolo de medida (para que las próximas cifras sí sean trazables):**

1. Compilar la demo en la config a medir: `bash ./tools/build/build-demo.sh demos/amiga/<demo> --debug` (o `--release`).
2. Poblar el `runner.uae`/`dh1` de esa config ejecutándola una vez: `bash ./tools/run/run-demo.sh demos/amiga/<demo>`.
3. Medir con `node tools/debug/measure-fps.mjs <demo> <CONFIG_ID>` (usa el contador de ciclos del periférico `0xB7E928`, 7,09379 MHz).
4. Anotar junto a cada cifra: **fecha, `CONFIG_ID`, commit (`git rev-parse --short HEAD`) y `detail`** (fase).

**Estado del intento de re-medición (2026-09-17):** al ejecutar el protocolo sobre `101_ehb_tile_scroll_driver` (`A500_debug`), la medida salió inválida (`detail=0x0` y el contador de frames retrocedió, señal de que la máquina no llegó a READY): `measure-fps.mjs` lanzó WinUAE con la config por defecto de la extensión y no ejecutó la demo del `dh1`. Queda pendiente corregir el arranque del runner para esa medición antes de poder fijar la tabla trazable.
