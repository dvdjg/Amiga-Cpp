# Carretera/suelo por raster (parcheo de `BPLxPT` por línea)

**Qué resuelve**: un **suelo o carretera que se desplaza** con **perspectiva**, **curvas** y **cambios de rasante** (colinas), pintado casi todo por el **Copper**. Es la técnica de *Lotus*, *OutRun* (ports) o *Crazy Cars*: el CPU recalcula unas **tablas** por frame y el Copper re-apunta el bitplane **en cada scanline**; casi no hay coste de 68000 en el pintado.

## Cómo funciona

1. **Buffer ancho planar** (Chip RAM): una "textura" de carretera con **todas las anchuras**, más ancha que la pantalla (x2 o más). En **planar contiguo** (cada plano en su bloque) para que el Copper solo cambie un puntero por plano. Dirección de una línea:
   `plane[p] + y_source * bytes_por_fila_del_plano + (x_offset >> 3)`.
2. **Tabla vertical `y_source[y]`** (por scanline de pantalla): qué línea del buffer mostrar. Codifica la **perspectiva** (hacia el horizonte se saltan más líneas) y la **rasante** (el mapa de alturas repite/desplaza líneas).
3. **Tabla horizontal `x_offset[y]`**: desplazamiento dentro del buffer ancho. Codifica **curvas** (se acumula con la profundidad) y el **scroll**.
4. **Copper list**: por cada scanline de la banda de suelo, `WAIT` a la línea + `MOVE BPLxPTH/BPLxPTL` (un par por plano) apuntando a la línea calculada. Opcional: `BPLCON1` para el **scroll fino** (0–15 px).
5. **Por frame**: recalcular `y_source`/`x_offset` (perspectiva 1/z + curvas + alturas) y **parchear** los punteros. El resto lo hace el Copper.

## Coste y límites

- **Copper por línea**: cada scanline con par de `MOVE` por plano consume Copper y **anchura de bus**; una banda de ~150 líneas × 4 planos es la parte a vigilar (`copper-timing-and-budget.md`). Mitigación clásica: parchear **cada 2 líneas** y usar **`BPLxMOD`** para repetir la línea (`modulo-tricks.md`).
- **Fetch**: el buffer ancho no puede exceder el límite de fetch por línea del playfield.
- **División 1/z**: el punto caro en 68000; precalcular **tablas de recíprocos** o usar `div_wide` (`core/affine.hpp`).
- **Rasantes**: con `y_source` muy repetido cerca del horizonte, `BPLxMOD` negativo ahorra ancho de banda.

## En el engine

**Se reutiliza** (no hay que reinventarlo):

| Pieza | Dónde |
|---|---|
| Parcheo barato de un `MOVE` por frame (**1 store**) | `copper::PatchHandle` (`scheduler.hpp:72-85`, HOST-214) |
| Par de puntero de 32 bits (`BPLxPTH`+`BPLxPTL`) parcheable | `Patch32`/`patch32_at` (`composition/compose.hpp:84-98`) |
| Escribir un puntero de plano | `Scheduler::move_bitplane_pointer` (`scheduler.hpp:211`), `ListBuilder::move_bitplane_pointer` (`copper.hpp:188`) |
| Doble buffer de copperlist + estructura fija con slots | `copper::DoubleBuffer` (`double_buffer.hpp:49`), `copper::Template` (`template.hpp:34`) |
| **Repetir/saltar líneas** con `BPLxMOD` | `compose.hpp::row_repeat` (`:893-908`) |
| Scroll fino (`BPLCON1`) y DDF | `effects::FineScroll` (`api/effects.hpp:309`), `graphics/playfield_scroll.hpp:20-32` |
| Emitir splices de bitplane en líneas dadas | `Scheduler::emit_copper_intents_full` (`scheduler.hpp:578`; `BitplaneSplit`/`ShiftLines` :638/:648) |

**Falta**: un **driver de suelo por línea** que, desde las tablas, emita `WAIT + par BPLxPT` en **cada** scanline de la banda (hoy solo hay splits **puntuales**, en `xlimited_composer.hpp:219-231` y `emit_mode_switch_zone`). Y, si se quiere declarar como intención, materializar `BitplaneSplit`/`ShiftLines` en el `Plan` (hoy dan 0 palabras, `compose.hpp:836-839`).

## Trampas

- **Planar contiguo** para el buffer (con interleaved, cada plano exigiría su propio cálculo de módulo).
- **Recalcular tablas en VBlank** y parchear la lista **inactiva** (doble buffer); no tocar la activa.
- Precisión: usar **fixed-point** (16.16) o tablas de recíprocos; la división flotante por línea no cabe en el presupuesto.
- No confundir el **scroll grueso** (bytes, `x_offset >> 3`) con el **fino** (`BPLCON1`, `x_offset & 15`).

## Enlaces

- **OutRun Amiga Edition — Making Of, cap. 1 «The Long Road»** (explicación moderna del *road* del arcade con Copper): <https://reassembler68k.itch.io/outrun-amiga-edition/devlog/1029155/the-making-of-outrun-chapter-1-the-long-road>
- **amiga-bootcamp — Copper Effects** (splits de raster, cambios de punteros): <https://github.com/alfishe/amiga-bootcamp/blob/main/17_demoscene/copper_effects.md> — copia local en `../amiga-bootcamp/17_demoscene/copper_effects.md`.
- **`demoscene-repo`**: efectos de origen aún **no iniciados** `effects/floor` (`DX08_floor_scroll`, «muy interesante para scroll y `BPLCON1` por línea») y `effects/highway` (`DX13_highway`) — ver `docs/demos/effects/demoscene-repo-coverage-index.md:42,47`.
- Búsquedas sugeridas: *Amiga copper road effect*, *Lotus Turbo Challenge copper*, *OutRun Amiga copper list*.
- **Aportado por el usuario** (esquema de tablas `y_source`/`x_offset`, fixed-point y parcheo): sirvió de base a esta ficha; el plan está en [`ROADMAP_RASTER_ROAD.md`](../../guides/roadmap/ROADMAP_RASTER_ROAD.md).

## Referencias

- AHRM 3.ª: cap. del Copper (WAIT/MOVE, `BPLxPT`), `BPLCON1` (`docs/reference/ahrm/`).
- Fichas locales: [`modulo-tricks.md`](modulo-tricks.md), [`copper-timing-and-budget.md`](copper-timing-and-budget.md), [`copper-chunky.md`](copper-chunky.md).
- Plan por fases: [`ROADMAP_RASTER_ROAD.md`](../../guides/roadmap/ROADMAP_RASTER_ROAD.md).
