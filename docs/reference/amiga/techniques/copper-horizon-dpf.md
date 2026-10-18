# Horizonte cielo/tierra por offset por scanline (DPF 2+3)

**Qué resuelve**: un **horizonte inclinado y móvil** (cielo arriba, tierra abajo, con pendiente y desplazamiento por cámara) **sin rellenar nada cada frame**: el fondo vive en el playfield de atrás (PF2) de un **dual playfield** y el Copper, por scanline, elige una de dos líneas patrón de 640 px y su offset horizontal para que la frontera tierra↔cielo caiga exactamente en `x_h(y)`. El PF1 (3 planos, hasta 7 colores opacos + transparente) queda entero para la escena 3D poligonal. Es el truco de juegos con horizonte tipo *F29 Retaliator* / *Castle Master* y encaja con la técnica de [fotogramas intermedios por paneo](camera-pan-fake-frames.md) (el horizonte se corrige «de verdad» cada frame aunque la geometría de PF1 vaya paneada).

## Modelo DPF 2+3

| Playfield | Planos de hardware | Colores | Uso |
|---|---|---|---|
| **PF1** (delante) | BPL1, BPL3, BPL5 (índices pares en base 0) | 0–7, con **0 transparente** | escena 3D (`PlaneFillBuilder` + Blitter) |
| **PF2** (detrás) | BPL2, BPL4 (índices impares) | **8–11**, con **8 transparente** | cielo/tierra |

Planos pares/impares y transparencia: AHRM 3.ª (`docs/reference/ahrm/…cat.md:1990`: «color 0 in playfield 1 and color 8 in playfield 2»); `BPLCON0` con `DBLPF` (`scene/display.hpp`, `Band::bplcon0()`). `BPLCON1` en DPF tiene **un nibble por playfield** (bajo = PF1, alto = PF2), lo que da al fondo scroll fino **independiente** del paneo fake de PF1 (`graphics/drivers/tile_scroll.hpp:22-24`, ya usado en las demos 110/111/112/202).

## Las dos líneas patrón de 640 px

En Chip RAM, horneadas una sola vez (2 planos × 80 B por línea):

```text
Línea A:  [=== TIERRA ×320 ===][=== CIELO ×320 ===]   (frontera en buffer x=320)
Línea B:  [=== CIELO  ×320 ===][=== TIERRA ×320 ===]   (patrón complementario)
```

Con la convención de scroll del engine —puntero al coarse `(scroll_x−1) & ~15`, `DDFSTRT=$30` adelantado una word y `fine_delay(scroll_x)` en el nibble de PF2 de `BPLCON1` (`engine/include/eng/graphics/playfield_scroll.hpp:20-34`)— la pantalla empieza en la posición `scroll_x` del búfer, así que la frontera (búfer x=320) se ve en pantalla en `split = 320 − scroll_x`. Para poner el horizonte en `x_h`:

```text
scroll_x = 320 − x_h      (clampear a [1..320] → x_h representable en [0..319])
línea A si la tierra queda a la izquierda; línea B si el par cielo/tierra se invierte
```

Con 640 px de búfer y ventana de 320 px el split alcanza **±160 px** respecto al centro; pendientes mayores o splits fuera de rango se clampean (o se ensancha el búfer a 768/1024, a cambio de más fetch y margen).

## Bucle por frame

1. De la cámara: `y0` (de pitch) y `slope` (de roll/pendiente, px por línea). Con pendiente constante, `x_h(y)` se **acumula** por línea (`x += slope`), sin multiplicaciones.
2. Se **emite una vez** la plantilla de la banda: por línea `WAIT` + los `MoveSlot` de los dos punteros de PF2 (`BPL2PT`,`BPL4PT`) y el de `BPLCON1` (patrón «emitir con `move_at` y guardar slots» de `TileScrollScene`, `tile_scroll.hpp:748-810`; `ListBuilder::wait_line_pal` maneja las líneas >255).
3. Por frame se **parchean solo los datos** que cambian (doble buffer de copperlist, bloque inactivo):
   - el nibble fino de PF2 cambia **cada línea** (1 store por línea);
   - el coarse de los dos punteros cambia **cada 16/slope líneas** (por eso el coste real de parcheo es pequeño: la parte gruesa se mantiene en tramos largos);
   - la elección A/B cambia a lo sumo 1 vez con un horizonte recto.
4. El `clear`/relleno de la escena 3D de PF1 no toca PF2: el fondo no se repinta nunca.

## Coste y límites

- **Copper**: la plantilla ocupa ~12 words por línea (WAIT + 2 planos × PTH/PTL + `BPLCON1`); 256 líneas ≈ 3 k words ≈ 6 KB por bloque — cabe en `PlanConfig::copper_bytes` (8 KB por bloque, `copper/plan.hpp:57`) pero compite con el resto de la lista (sprites, cabecera de PF1). Alternativas si aprieta: parchear/emitir **cada 2 líneas** (pendiente más tosca) o limitar la banda al tramo donde el horizonte es visible. Presupuesto de instrucciones por línea y contienda de bus: [`copper-timing-and-budget.md`](copper-timing-and-budget.md).
- **DMA**: 5 planos de fetch por línea (2 de PF2 + 3 de PF1) — legal en OCS a 320 px; vigilar el margen de fetch (el búfer ancho no puede exceder el límite por línea del playfield).
- **Casos límite**: horizonte casi horizontal ⇒ variante aún más barata (un `MOVE` de `COLOR` a la altura `y0`, sin tocar punteros); cámara volcada o split fuera de `[0..319]` ⇒ clampeo y línea B.
- **Relación con el paneo fake**: el horizonte de PF2 se recalcula con la cámara real cada frame; el scroll fino es **por playfield**, así que el paneo de PF1 (ventana fake) no lo arrastra. En un playfield único de 5 planos, en cambio, el fino es compartido y el paneo movería también el fondo.

## Los colores libres de PF2 (índices 8–11)

| Índice | Rol sugerido |
|---|---|
| 8 | Transparente (deja ver `COLOR00`/fondo) o neblina |
| 9 | Tierra |
| 10 | Cielo |
| 11 | Haze/sol/acento horneado en la línea (p. ej. 8–32 px en la frontera, o un bloque en la mitad del cielo que se mueve con el scroll y por tanto con el yaw) |

Gradiente de cielo **sin más índices**: mover `COLOR10` por bandas de Y con el mismo Copper (patrón ya usado en la 125 con `COLOR01..06`/`09..13`, `docs/demos/effects/dx39-layers-original-analysis.md:194`). El «suelo con trama cerca» no entra aquí: eso es relleno de PF1 o un trapecio con `PatternFill`.

## Alternativa de 1 plano en un framebuffer de 5 (estilo RoboCod)

- El **coarse** sí es independiente por plano (el Copper escribe solo el `BPLxPT` del plano de fondo), pero el **fino no**: en playfield único `BPLCON1` es compartido por todos los planos activos. Un horizonte inclinado fino pediría offsets por plano que el hardware no da.
- El engine ya recorrió ese camino: en el rediseño de la 112 (RoboCod de 5 planos) un blit de FG que no salta el plano de fondo **borra** el fondo en las filas que entran; la solución limpia fue **DPF de 2 capas** con parallax (`robocod-layered-scroll.md`, `XLimitedPlayfield::parallax_plane`). Para el horizonte, DPF 2+3 es el camino principal; el truco RoboCod se reserva a su caso (parallax de un fondo a speed distinta dentro de un mismo mapeo de 16 colores).

## Falta de engine (compartido con la carretera por raster)

El driver genérico que emite `WAIT + par BPLxPT` en **cada scanline de una banda** todavía no existe: hoy solo hay splices puntuales (`xlimited_composer.hpp`, `emit_mode_switch_zone`). La ficha [`copper-road-rasters.md`](copper-road-rasters.md) documenta la misma carencia para el suelo con perspectiva; **un solo driver por-scanline** debe servir a las dos técnicas (tablas de `y_source`/`x_offset` para la carretera; tabla A/B + `scroll_x(y)` para el horizonte). Plan: [`ROADMAP_3D_PRESENTACION.md`](../../../guides/roadmap/ROADMAP_3D_PRESENTACION.md) (fase del horizonte) y [`ROADMAP_RASTER_ROAD.md`](../../../guides/roadmap/ROADMAP_RASTER_ROAD.md) (carretera).

## Referencias

- AHRM 3.ª: dual playfield (transparencia `:1990`), `BPLCON1`/`BPL2MOD`, Copper (`WAIT`/`MOVE`).
- Fichas locales: [`copper-road-rasters.md`](copper-road-rasters.md), [`copper-timing-and-budget.md`](copper-timing-and-budget.md), [`dual-layer.md`](dual-layer.md), [`robocod-layered-scroll.md`](robocod-layered-scroll.md).
- Engine: `engine/include/eng/graphics/playfield_scroll.hpp` (convenciones de fine/coarse), `engine/include/eng/graphics/drivers/tile_scroll.hpp` (DPF 2+3 con patch por frame), `engine/include/eng/graphics/copper/` (`ListBuilder`, `DoubleBuffer`), `engine/include/eng/scene/display.hpp` (`Band`/DPF).
