# Roadmap — carretera/suelo por raster (`BPLxPT` por línea)

Plan para implementar en el engine el efecto de **suelo/carretera desplazándose con perspectiva,
curvas y rasantes** por parcheo de los punteros de bitplane **en cada scanline**. La técnica y los
enlaces están en [`docs/reference/amiga/techniques/copper-road-rasters.md`](../../reference/amiga/techniques/copper-road-rasters.md).

## Principios

- **El efecto declara, el driver emite.** El cálculo vive en el dominio (tablas `y_source`/`x_offset`
  por línea, puro y host-testable); la emisión de `WAIT`+`BPLxPT` la hace un *driver* que conoce el
  Copper (patrón de `PLAYFIELD_SCROLL_ARCHITECTURE.md`).
- **Casi todo lo hace el Copper.** El CPU recalcula tablas y parchea; el pintado por línea es DMA.
- **Reutilizar el Copper del engine** (§1.6): `Scheduler`/`PatchHandle`/`Patch32`, `DoubleBuffer`,
  `Template`, `row_repeat`, `FineScroll`. No crear una copperlist paralela.
- **HOST primero**: las tablas y la **emisión** de la lista son verificables en host (comparando la
  copperlist emitida, como en HOST-214/215); el hardware cierra con demo.
- **Fixed-point y presupuesto**: nada de `float` por línea; precalcular recíprocos y respetar el
  presupuesto de Copper/bus (`copper-timing-and-budget.md`).

## Estado de partida

**Existe (se reutiliza):** `Scheduler::move_bitplane_pointer` (`scheduler.hpp:211`), `PatchHandle`
(`:72-85`, HOST-214), `Patch32` (`compose.hpp:84-98`), `DoubleBuffer` (`double_buffer.hpp:49`),
`Template` (`template.hpp:34`), `row_repeat` (`compose.hpp:893`), `FineScroll`
(`api/effects.hpp:309`), `emit_copper_intents_full` con `BitplaneSplit`/`ShiftLines`
(`scheduler.hpp:578`) y el buffer planar ancho (`contiguous_playfield.hpp:25`, `plane_stride`).

**Falta:** un **driver de suelo por línea** (hoy solo hay *splits puntuales*,
`xlimited_composer.hpp:219-231`) y, si se quiere declarativo, materializar
`BitplaneSplit`/`ShiftLines` en el `Plan` (`compose.hpp:836-839` da 0 palabras). No hay precedente de
*pseudo-3D* de carretera en el repo.

**Precondición (política §de efecto nuevo):** analizar los efectos de origen del `demoscene-repo`
`effects/floor` (`DX08_floor_scroll`) y `effects/highway` (`DX13_highway`) y anotarlos en
`docs/demos/effects/demoscene-repo-coverage-index.md:42,47` antes de escribir código.

## Diseño objetivo

```text
  RoadTables (puro, HOST)           RoadRasterDriver (emite Copper)
  ┌───────────────────────┐         ┌───────────────────────────────┐
  │ y_source[linea]       │         │ por cada linea de la banda:   │
  │ x_offset[linea]       │  ───▶   │  WAIT(linea)                  │
  │ (curva, rasante, z)   │         │  MOVE BPLxPTH/L = base + ...   │
  └───────────────────────┘         │  (opcional BPLCON1 fino, MOD) │
                                     └───────────────────────────────┘
```

- **Buffer**: `field::ContiguousPlayfield` ancho (`ROAD_W > SCREEN_W`); puntero de línea
  `plane[p] + y_source * row_stride_plano + (x_offset >> 3)`.
- **Driver**: recorre la banda emitiendo `WAIT`+par `BPLxPT` por línea (con `move_bitplane_pointer`),
  y deja **slots parcheables** para el frame siguiente (`PatchHandle`/`Patch32`), sobre la lista
  **inactiva** de un `DoubleBuffer`.

## Fases

### R0 — Driver de suelo por línea (núcleo)

- **Entregable**: `RoadTables` (relleno desde fuera) + `emit_road_band(scheduler, tables, planes,
  row_stride, …, línea0, n)` que emite `WAIT+BPLxPT` por línea; una banda **estática** (una imagen de
  suelo, sin movimiento) montada en una copperlist con `DoubleBuffer`.
- **Verificación**: **HOST** — la lista emitida contiene, por línea, su `WAIT` y el par `BPLxPT` con
  la dirección esperada (patrón HOST-214/215); demo mínima que muestra la banda estática en hardware.
- **Estado**: pendiente.

### R1 — Scroll horizontal (buffer ancho + `x_offset`)

- **Entregable**: `x_offset[y]` constante + desplazamiento por frame → el suelo **se desplaza**;
  manejo del **wrap** (el buffer es más ancho, pero acotado).
- **Verificación**: **HOST** de la tabla y del parcheo; **demo** con suelo desplazándose.
- **Estado**: pendiente.

### R2 — Perspectiva (`y_source` con 1/z)

- **Entregable**: `y_source[y]` por **perspectiva** (recíprocos precalculados, fixed-point);
  horizonte y `clamp`.
- **Verificación**: **HOST** — la tabla `y_source` es monótona y comprime hacia el horizonte; demo
  con profundidad.
- **Estado**: pendiente.

### R3 — Curvas (`x_offset` acumulado)

- **Entregable**: acumulación de la curvatura con la profundidad (offset por línea) → **curva**;
  suavizado.
- **Verificación**: **HOST** — una curva introduce un desplazamiento creciente con la profundidad;
  demo con curvas.
- **Estado**: pendiente.

### R4 — Rasantes (mapa de alturas + `BPLxMOD`)

- **Entregable**: `height_map` indexado por Z → `y_source` desplazado (colinas); uso de
  `row_repeat`/`BPLxMOD` donde se repitan líneas, para ahorrar ancho de banda.
- **Verificación**: **HOST** — una colina sube/baja el horizonte; demo con rasantes.
- **Estado**: pendiente.

### R5 — Scroll fino y presupuesto

- **Entregable**: `BPLCON1` fino (0–15) por línea desde `x_offset`; parcheo **cada 2 líneas** +
  `BPLxMOD` cuando el presupuesto apriete; medición del coste con el `ScheduleReport` del scheduler.
- **Verificación**: **HOST** del nibble de `BPLCON1`; **demo** con el presupuesto documentado en
  `copper-timing-and-budget.md`.
- **Estado**: pendiente.

### R6 — Bucle de juego (coche, horizonte, sprites)

- **Entregable**: cámara/z/curva entrando desde la lógica de juego; **sprite** del coche (sprites de
  hardware) y banda de **horizonte** (paleta/estático); recálculo de tablas en VBlank (doble buffer).
- **Verificación**: **demo** jugable (acelerar, girar, subir/bajar).
- **Estado**: pendiente.

### R7 — Integración declarativa (intención)

- **Entregable**: materializar `BitplaneSplit`/`ShiftLines` en el `Plan`
  (`compose.hpp:836-839`), para que la carretera aporte sus splits **como intención** ordenada por el
  `Plan` en vez de emitirlos a mano.
- **Verificación**: **HOST** (la intención materializa las mismas palabras que el driver directo).
- **Estado**: pendiente.

## Tests y demos previstos

| ID | Tipo | Contenido |
|---|---|---|
| HOST-R0 | test | Emisión `WAIT+BPLxPT` por línea desde una tabla (comparada palabra a palabra). |
| HOST-R2 | test | `y_source` por perspectiva (monótona, comprimida hacia el horizonte). |
| HOST-R3 | test | `x_offset` acumulado (curva) crece con la profundidad. |
| HOST-R4 | test | `height_map` → desplazamiento del horizonte + `row_repeat`. |
| HOST-R5 | test | Nibble de `BPLCON1` por línea desde `x_offset`. |
| HOST-R7 | test | La intención materializada == el driver directo. |
| demo | demo | `NNN_raster_road` (número libre del bloque D; `node tools/check/next-number.mjs`). |

## No-objetivos

- **No** es un motor 3D: es una banda de suelo por raster; la geometría es 2.5D (tablas), no mallas.
- **No** sustituye al scroll por tiles (`ScrollEngine`) ni al *cork screw* (`xlimited`): son
  composiciones distintas; convivirá con ellas si hace falta (HUD, cielo).
- **No** pretende copperlist propia: usa el `Scheduler`/`DoubleBuffer` del engine.

## Riesgos

- **Presupuesto de Copper por línea** (el riesgo principal): mitigar parcheando cada 2 líneas +
  `BPLxMOD`; medir en R5 antes de escalar la banda.
- **Wrap** del buffer ancho: con curvas grandes el `x_offset` puede salirse; resolver con un buffer
  con margen (ROAD_W extra) y módulo.
- **División 1/z**: sin tablas de recíprocos no cabe en el frame.

## Referencias

- Técnica + enlaces: [`copper-road-rasters.md`](../../reference/amiga/techniques/copper-road-rasters.md)
  (incluye el *making-of* de OutRun Amiga y `amiga-bootcamp/copper_effects.md`).
- Base del engine: [`PLAYFIELD_SCROLL_ARCHITECTURE.md`](../../engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md),
  [`modulo-tricks.md`](../../reference/amiga/techniques/modulo-tricks.md),
  [`copper-timing-and-budget.md`](../../reference/amiga/techniques/copper-timing-and-budget.md).
- Origen demoscene: `docs/demos/effects/demoscene-repo-coverage-index.md` (`08 Floor`, `13 Highway`).
