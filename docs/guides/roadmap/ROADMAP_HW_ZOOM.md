# Roadmap — zoom hardware (`BPLxMOD` + `BPLCON1`)

Plan para implementar en el engine los **zooms por hardware** (vertical por módulos, horizontal por
`BPLCON1` a mitad de línea, pre-escalados y Blitter). La técnica y los enlaces están en
[`docs/reference/amiga/techniques/hardware-zoom.md`](../../reference/amiga/techniques/hardware-zoom.md).

## Principios

- **El efecto declara, el driver emite** (igual que la carretera): el cálculo (tabla de módulos,
  magic table, selección de versión) es dominio puro y host-testable; la emisión de Copper la hace un
  *driver* que conoce los registros.
- **Reutilizar el Copper del engine** (§1.6): `Scheduler`/`PatchHandle`, `DoubleBuffer`, `row_repeat`,
  `wait_position`, `FineScroll`. Nada de copperlist propia.
- **No duplicar el rotozoom**: el rotozoom **software** (CPU+C2P) ya existe
  (`graphics/effects/rotozoom.hpp`, `effects::Rotozoom`); el zoom **hardware** es otra vía (para
  zooms grandes y suaves sin coste de CPU).
- **Timing y presupuesto**: el truco horizontal es crítico; medir con `copper-timing-and-budget.md`.
- **HOST primero**: la copperlist emitida (WAIT/MOVE por línea) es verificable en host (HOST-214/215);
  el hardware cierra con demo.

## Estado de partida

**Existe:** `row_repeat` (`compose.hpp:893`, `BPLxMOD` por línea), `Scheduler::wait_position(_safe)`
(`scheduler.hpp:242/251`), `PatchHandle` (`:72-85`), `FineScroll`/`playfield_scroll.hpp`,
`DoubleBuffer` (`double_buffer.hpp:49`), y el **rotozoom software** (`rotozoom.hpp`/`Rotozoom`,
HOST-132, ASM `support/rotozoom_loop.s`).

**Falta:** la **tabla de módulos por línea** (más allá de una pasada de `row_repeat`), el truco
**`$102`** (magic table + escritura de `BPLCON1` a mitad de línea), y los backends de
**pre-escalados** y **Blitter zoom**. No hay ficha ni efecto de zoom hardware previo.

## Diseño objetivo

```text
  ZoomTables (puro, HOST)                ZoomDriver (emite Copper)
  ┌────────────────────────┐             ┌─────────────────────────────────┐
  │ mod[y]  (BPLxMOD)      │             │ copperlist fija (WAIT + MOVE):  │
  │ bplcon1[y] / magic[]   │  ───▶       │  BPL1MOD/BPL2MOD  <- mod[y]     │
  │ version (pre-escalado) │             │  BPLCON1(mitad de linea) <- magic│
  └────────────────────────┘             │  BPLxPT <- version/puntero      │
                                          └─────────────────────────────────┘
```

## Fases

### Z0 — Zoom vertical por módulos

- **Entregable**: `ZoomVertical` (tabla de módulos fixed-point `mod[y]` desde `zoom_fp`) + un driver
  que emite **una copperlist fija** (`WAIT` + par `MOVE` a `BPL1MOD`/`BPL2MOD` por línea, con slots
  parcheables) y por frame solo **parchea los valores** (`PatchHandle`).
- **Verificación**: **HOST** — `mod[y]` correcto para varios factores (256/512/128) y la lista
  emitida con sus `MOVE` en los slots esperados; **demo** con una imagen que “respira” (zoom vertical).
- **Estado**: pendiente.

### Z1 — Centrado y convivencia con el scroll

- **Entregable**: recentrado por `DIWSTRT`/`DIWSTOP` (o por el origen de los punteros), parametrizable;
  convivencia con `FineScroll`/scroll de fondo.
- **Verificación**: **HOST** de las constantes de recentrado; **demo** con zoom centrado.
- **Estado**: pendiente.

### Z2 — Zoom horizontal (`$102` / magic table)

- **Entregable**: la **magic table** (valor de `BPLCON1` + posición horizontal del `WAIT` para ocultar
  1–15 píxeles) como tabla del engine + la emisión de la escritura **a mitad de línea**
  (`wait_position` + `move`), con la **limitación a 4 planos** y la **compensación** del
  desplazamiento a la izquierda.
- **Verificación**: **HOST** — para cada “ocultar n”, la lista contiene el `WAIT` de posición y el
  `MOVE BPLCON1` con el valor de la tabla; **demo** que reduce el ancho de una imagen en pasos.
- **Estado**: pendiente. **Depende de**: fijar los valores de la tabla desde el artículo (o derivarlos
  y documentarlos) — no inventar valores (§1.7).

### Z3 — Pre-escalados + Copper

- **Entregable**: `PreScaledZoom` — N versiones de una imagen (generadas en host/tool) como planos
  contiguos + selección por Copper de la versión más cercana, más ajuste fino con módulos.
- **Verificación**: **HOST** de la selección (índice por zoom); **demo** con zoom suave y limpio.
- **Estado**: pendiente.

### Z4 — Blitter zoom (objetos)

- **Entregable**: driver de zoom por Blitter (copia línea a línea con anchos distintos) para zonas
  pequeñas (objetos/paneles); reutiliza el Blitter del engine.
- **Verificación**: **HOST** (geometría de la tabla de líneas); **demo** con un objeto que se acerca.
- **Estado**: pendiente.

### Z5 — Híbrido e integración con la carretera

- **Entregable**: combinar **módulos (Z0) + `BPLCON1` (Z2)** y, en la copperlist de la carretera
  ([`ROADMAP_RASTER_ROAD.md`](ROADMAP_RASTER_ROAD.md)), añadir los slots `BPLxMOD`/`BPLCON1` a la lista
  por línea (`BPLxPT` + módulos + `BPLCON1` en la **misma** copperlist).
- **Verificación**: **HOST** (la lista combinada emite los tres tipos de registro por línea); **demo**
  con la carretera estirándose/comprimiéndose.
- **Estado**: pendiente.

## Tests y demos previstos

| ID | Tipo | Contenido |
|---|---|---|
| HOST-Z0 | test | Tabla de módulos por línea (`zoom_fp` 256/512/128) + emisión de la lista. |
| HOST-Z1 | test | Constantes de recentrado (DIWSTRT/DIWSTOP/origen) para varios zoom. |
| HOST-Z2 | test | Magic table + `WAIT` de posición horizontal + `MOVE BPLCON1` por línea. |
| HOST-Z3 | test | Selección de la versión pre-escalada por factor de zoom. |
| HOST-Z4 | test | Tabla de líneas del Blitter zoom (src_y/src_w por línea). |
| demo | demo | `NNN_hw_zoom` (número libre del bloque D; `node tools/check/next-number.mjs`). |

## No-objetivos

- **No** sustituye al **rotozoom software** (existe): el rotozoom rota *y* escala con CPU+C2P; el zoom
  hardware **no rota**.
- **No** es un escalador general: el vertical por módulos solo repite/salta líneas; el horizontal
  está limitado a ±15 px por grupo y a **≤4 planos**.
- **No** pretende pantalla completa por Blitter (caro); el Blitter se reserva a objetos.

## Riesgos

- **Timing del `$102`**: la posición horizontal del `WAIT` es exacta y sensible a PAL/NTSC y al modelo;
  validar en hardware con `copper-timing-and-budget.md`.
- **Valores de la magic table**: no son “redondos”; hay que derivarlos/documentarlos desde la fuente
  (artículo/ZIP) antes de fijarlos.
- **Copper por línea**: añadir `MOVE` de módulos (2 por línea) a la carretera suma presupuesto; medir.

## Referencias

- Técnica + enlaces (Obligement, Stash of Code, Sanity, zip de fuentes): [`hardware-zoom.md`](../../reference/amiga/techniques/hardware-zoom.md).
- Fichas: [`modulo-tricks.md`](../../reference/amiga/techniques/modulo-tricks.md),
  [`copper-timing-and-budget.md`](../../reference/amiga/techniques/copper-timing-and-budget.md),
  [`copper-road-rasters.md`](../../reference/amiga/techniques/copper-road-rasters.md).
- Rotozoom existente: `engine/include/eng/graphics/effects/rotozoom.hpp` (HOST-132).
- Carretera: [`ROADMAP_RASTER_ROAD.md`](ROADMAP_RASTER_ROAD.md) (Z5 se integra con ella).
