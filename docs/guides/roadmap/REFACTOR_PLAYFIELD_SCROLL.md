# Roadmap: refactor de Playfields y Scroll (separación de conceptos)

Plan por fases para llevar el código desde el estado actual (acoplado) al **modelo objetivo** de
`docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md`. Objetivo: que algoritmo, superficie,
composición y mapping de máquina sean piezas independientes y testeables, sin romper ninguna demo
en el proceso.

## Principios del refactor

- **No big-bang**: cada fase deja build verde, tests host verdes y las demos existentes
  funcionando (idealmente vía adaptadores que se retiran en fases posteriores).
- **Sin virtuals en el hot path** (`CODING_STYLE.md`): estrategias y playfields por template/policy.
- **Evidencia** por fase: `build → run → analyze` y, si toca visual, secuencia + revisión crítica
  (`DEMO_VISUAL_DEBUG.md`).
- **Sin duplicar**: reutilizar `ScrollEngine` (ya es el algoritmo bueno), `Playfield`,
  `CanvasPlayfield`, `FramePlan`, backend.

## Fases

### Fase 0 — Contrato y red de seguridad
- Fijar en `PLAYFIELD_SCROLL_ARCHITECTURE.md` los contratos `ScrollView`, `ScrollTarget`,
  `ScrollEmitter` y las estrategias (§2–§4).
- Añadir/ajustar tests host que fijen el contrato ANTES de mover código:
  - `HOST-023` (ejes Ring/Finite) ya cubre el engine; ampliar a `ScrollTarget`/`Emitter`.
  - Nuevo test de `PlaneView` (vista de planos: mapeo, stride, doble buffer).
- Entregable: docs + tests (sin cambios de producción).

### Fase 1 — `PlaneView` + `SoftDpfComposition` (la que más “totum” quita)
- Unificar `Playfield<N>`: una sola clase con **política de layout** (`Flat`/`Ring`) y
  `ScrollStrategy` opcional. `CanvasPlayfield` pasa a ser `Playfield<N>` con `Layout=Flat` y sin
  scroll (no una clase aparte).
- Introducir `PlaneView` (superficie que referencia N planos de otro playfield, no posee memoria).
- Introducir `SoftDpfComposition`: FG (`Playfield<4>`) + BG (`PlaneView` de 1 plano sobre el
  bitmap del FG) con **doble buffer del plano de fondo** y puntero alterno.
- Mover fuera de `XLimitedPlayfield`: `parallax_plane`, `bg_flip`, `fill_parallax_pattern`,
  `make_bg_plane_copy_rect_job`/`bg_window_for`/`bg_split_rects`.
- **Piloto: demo 112.** Reducir a: FG scroll + BG (vista/doble buffer) + su cámara propia.
- Verificación: `K_DIAG_YONLY`+`K_DIAG_BG_FIXED` (borde constante), `analyze-sequence` (marcador y
  movimiento), `112_BG_FLICKER.md` sigue siendo la evidencia del flashing.
- Riesgo: medio. Toca `begin`/`hardware_view`/compositor single.

### Fase 1b — `ModeSwitchZone` (HUD/capas con menos planos)
- Añadir a la composición la capacidad de **cambiar la geometría de vídeo en una línea**
  (`BPLCON0`/`DDF`/`BPLxMOD` + punteros) para tramos con distinto número de planos.
- Caso de uso: juego de 5 planos arriba + HUD de 4/3/2 planos abajo, sin arrastrar la geometría.
- Verificar con **microtests** (invariante MI09 en `docs/reference/amiga/hardware/amiga-hardware-invariants-microtests.md`): HUD de 2/3/4 planos
  bajo un split sobre campo de 4/5/6; documentar el orden exacto de los MOVE del Copper.
- Mantener el modo conservador “misma geometría, conmutar punteros/paleta” como opción.

### Fase 2 — Algoritmo: estrategias y contrato target/emitter
- Oficializar `ScrollTarget` (layout del anillo) y `ScrollEmitter` (dibujo + seam) como conceptos.
- `RingScroll` con `AxisPolicy` (X/Y/XY) y `DirectionPolicy` (bi/one-way), sustituyendo
  `ScrollMode`/`AxisMode`.
- `BigBufferScroll`: estrategia trivial (solo puntero) para “escena ya dibujada”.
- Mantener `ScrollEngine` como implementación de `RingScroll` (o renombrarlo) sin cambiar su
  aritmética (invariantes §1.2 de `XYLIMITED_ALGORITMO_GENERICO.md`).
- Verificación: `HOST-023` + `verify-tile-scroll-modes.mjs` + demos 201/202.

### Fase 3 — Mapping Amiga fuera del playfield
- `ScrollView` neutral (cámara + viewport + geometría anillo + layout + doble buffer).
- `AmigaDisplayMapper`: `ScrollView → planeaddx/BPLCON1/BPLxMOD/punteros/split`.
- `hardware_view` deja de calcular registros; el compositor usa el mapper.
- Riesgo: medio (toca el compositor y puede introducir jitter; medir con `K_DIAG_BG` y fps).

### Fase 4 — `DisplayComposition` con capas
- Sustituir el par fijo `m_field[2]`/`bg()/fg()` y el HUD embebido por una **lista de capas con rol**
  (fondo / primer plano / HUD), cada una `Playfield` + estrategia opcional.
- `SingleComposition`, `EhbComposition`, `DpfComposition`, `SoftDpfComposition`.
- Mover `update_auto`/`effect`/`phase_frames` (path de validación) a la **demo** (`TourDriver`).
- Verificación: 107 (single + dual), 201 (EHB), 202 (DPF), y la 201 con HUD como capa.

### Fase 5 — Adaptar demos y limpiar nombres
- Adaptar las 6 demos afectadas: 107, 110, 111, 112, 201, 202 (hoy usan `XlimitedScene`/
  `XLimitedPlayfield`).
- Renombrar de forma inequívoca: `XLimitedPlayfield` → `Playfield<N>` (Layout=Ring)+`RingScroll`;
  `XlimitedScene` → `DisplayComposition`/escena de capas; `…Xlimited…` 8-way → `XYLimited`.
- Retirar adaptadores de compatibilidad creados en fases 1–4.

### Fase 6 — API pública y planner (la app no ve hardware)
- Introducir `engine/include/eng/api/`: `App`/`World`/`Layer`/`Camera`/`Actor`/`Effect` con tipos
  fuertes, handles y `eng::Span`; `init` asigna, `frame` no.
- Añadir el **planner** (`RenderCompiler`): de la descripción de capas/efectos deduce la composición
  (single/EHB/DPF/soft-DPF/HAM/chunky + `ModeSwitchZone`) y **falla rápido en init** si no cabe.
- **Eliminar las fugas** actuales en las demos: `hardware_view()`, `mapposx()`, `display_offset()`,
  `make_bg_plane_copy_*` pasan a ser internos o de `eng::debug::`; la app usa la cámara.
- Migrar las demos al API público (una a modo de ejemplo), manteniendo acceso interno para las
  herramientas de diagnóstico.
- Principios y ejemplos: `docs/engine/architecture/PUBLIC_API.md`. Regla en `CODING_STYLE.md`
  (frontera de API pública).

## Mapa de migración (código actual → objetivo)

| Actual | Objetivo |
|---|---|
| `Playfield` (base) | `Playfield<N>` (base unificada: memoria/vista + layout) |
| `XLimitedPlayfield<N>` | `Playfield<N>` (Layout=Ring) + `RingScroll` + `RingLayout`/`Emitter` |
| `ScrollEngine`/`ScrollSink` | `RingScroll`/`BigBufferScroll` + `ScrollTarget`/`ScrollEmitter` |
| `ScrollMode`/`AxisMode` | `AxisPolicy` + `DirectionPolicy` |
| `CanvasPlayfield` | `Playfield<N>` (Layout=Flat, ScrollStrategy=None) |
| `parallax_plane` + `bg_flip` + `make_bg_plane_copy_*` (en `XLimitedPlayfield`) | `PlaneView` (1 plano) + `SoftDpfComposition` (doble buffer) |
| `XlimitedScene` (`m_field[2]`, `bg()/fg()`, `hud`) | `DisplayComposition` con lista de capas con rol (+ `ModeSwitchZone`) |
| `XlimitedDisplayComposer`/`XlimitedDualComposer` | `SingleComposition`/`EhbComposition`/`DpfComposition`/`SoftDpfComposition` |
| `hardware_view` (planeaddx/bplcon1/bplmod) | `ScrollView` neutral + `AmigaDisplayMapper` |
| `XlimitedPathConfig` (`update_auto`/`effect`) | driver de la **demo** (`TourDriver`) |

## Inventario de consumidores a adaptar

| Demo | Uso actual | Fase de migración |
|---|---|---|
| `107_xlimited_corkscrew` | corkscrew/DPF showcase, single+dual | 4–5 |
| `110_ylimited_shooter` | YLimited + DPF 3+3 | 4–5 |
| `111_xlimited_sidescroller` | XLimited (X ring, Y fijo) | 4–5 |
| `112_xlimited_robocod` | soft DPF (BG 1 bit + doble buffer) | **1** (piloto), 4–5 |
| `201_ehb_map` | EHB 6 planos + HUD + tour | 4–5 |
| `202_xlimited_dpf` | DPF 3+3 + parallax + Y por campo | 4–5 |

Infra compartida a revisar: `xlimited_scene.hpp`, `xlimited.hpp`, `xlimited_scene`,
compositores, `tile_scroll.hpp` (103/104) y `TileLayerMap`/`tile_map.hpp`.

## Criterios de aceptación (global)

- Cada fase: `build --debug/--release` OK, tests host verdes, las 6 demos + 201/202 analizan OK.
- Backend y `ScrollEngine` **sin regresión de rendimiento** (fps/ciclos medidos con `K_DIAG_*`).
- El soft DPF (112) queda **tear-free** (borde constante en solo-Y con fondo fijo).
- Documentación: `PLAYFIELD_SCROLL_ARCHITECTURE.md` es la referencia única; el resto de docs
  enlazan a él y no describen un modelo paralelo.

## Riesgos

- **Jitter/tearing**: el mapping Amiga y la composición están entrelazados con el timing; mover la
  responsabilidad puede reintroducir el flash de 1 px (`docs/debugging/112_BG_FLICKER.md`). Medir
  en cada fase.
- **Rendimiento**: el inventario de `hardware_view` es barato; moverlo al mapper no debe añadir
  divisiones/modulos por frame (cuidar `fast_div`).
- **Compatibilidad de demos**: usar adaptadores temporales; no reescribir las 6 demos de golpe.

## Estado

- 2026-09: **propuesta**. Fase 1 (piloto 112) implementada parcialmente dentro de
  `XLimitedPlayfield` (doble buffer del plano de fondo con 1 bitmap extra + `bg_flip`) — a extraer
  a `PlaneView`/`SoftDpfComposition` en la fase 1 formal.
