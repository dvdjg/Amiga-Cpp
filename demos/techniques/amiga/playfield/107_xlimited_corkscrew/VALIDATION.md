# VALIDATION — Demo 107 (`xlimited_corkscrew`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: ARRANCA (init ~45 s) — rendimiento del arranque pendiente** — deuda abajo.

Intención esperada: scroll XYLimited corkscrew (dos ejes + split), 50 fps.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| run (tras los arreglos 2026-10) | `run-demo.sh … --side-channel-timeout-ms 180000` | **RUN=0 / READY** en ~45 s emulados | arranca (antes: timeout a 40 s) |
| fps debug | `measure-fps` | **pendiente** (su espera de READY es más corta que el init) | medir tras bajar el init |
| sellos de ciclo del init (`g_init_stamps`, canal lateral) | `mem <addr> 20` | mapas **36,4k** · `scene.begin` **94,5k** · `apply_tech` **20,5k** · `fill` **31,8k** · **pre_scroll: >30 s (en curso al medir)** | el monstruo es `pre_scroll` |
| inspección en vivo (MCP, pausas) | `winuae_pause` | PC en bucles con `DIVU`/`memcpy`, progresando | init patológicamente largo, no cuelgue |

## Diagnóstico y arreglos aplicados (2026-10)

- **`build_map_coherent`**: `(x + y) % 97` por celda (80k celdas × `__umodsi3` ≈ 12M ciclos) →
  contador incremental por fila (una `%` por FILA). Medido: mapas = 36,4k ciclos ✓.
- **`pre_scroll` (engine, `xlimited_scene.hpp`)**: la fase X hacía 256 px-pasos por playfield
  (geometría del cruce por píxel); ahora usa **`scroll_burst_right_px`** en tramos de 24 px
  (equivalente, HOST-034). Nuevo método público `XLimitedPlayfield::scroll_burst_right_px`.
- **Lo que queda**: la fase **Y** del pre-scroll (`pre_scroll(256, 256)` → 256 pasos × 2
  playfields de `scroll_down` corkscrew) es el resto del coste (>30 s). Siguiente paso: un
  `burst_down`/`burst_up` en `ScrollEngine` (espejo de `burst_right`) o reducir el margen de
  `kPreScroll` (hoy 256 px). Herramienta: los sellos `g_init_stamps` (canal lateral `mem`).

## Diagnóstico histórico

- **Preexistente, no de este turno**: con el engine en HEAD (cambios del turno en `git stash`)
  falla igual; la última secuencia en `out/run/107…/sequence/` es del **2026-09-12**.
- `PC` de la primera muestra ($C15928) resuelto con `nm`: justo tras
  `eng::playfield::demo::pf_plane_row`; el `JSR` apuntaba a `memcpy` ($C1DA3E).

## Deuda registrada

DT-014: 107 con init de ~45 s (fase Y del pre-scroll). Medir fps tras el arreglo y pasar el
barrido de cadencias.
