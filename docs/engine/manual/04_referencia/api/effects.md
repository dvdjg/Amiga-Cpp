# Referencia — `effects` (`eng/api/effects.hpp`)

Efectos de **display** de alto nivel (`eng::effects`): el juego pide **el efecto**, no la secuencia de
Copper. Ver `docs/engine/architecture/EFFECT_MODEL.md`.

## `CopperChunky<MaxCols, MaxRows>`

Display **sin bitplanes** (`SceneMode::CopperChunky`): el color de cada fila lo escribe el Copper. El
juego escribe las palabras de color por fila.

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `init` | `bool init(Scene&, MemoryManager&, const DisplayLimits&, CopperChunkyConfig cfg, u16 width = 288, u16 height = 256, u32 copper_bytes = 12288)` | escena, memoria, límites, `{.cols,.rows}`, geometría | `false` si no cabe. |
| `begin_frame` | `void begin_frame(Scene&)` | — | — (destino = bloque inactivo). |
| `row` | `u16* row(u8 r) const` | fila | puntero a las palabras de color (paso 2 words). |
| `end_frame` | `void end_frame(Scene&, Backend&)` | — | — (flip + publica). |
| `cols`/`rows` | `u8 …() const` | — | geometría. |

## `Gradient`

**Degradado por banda** (paleta por líneas). Cumple el contrato `Effect`.

| Método | Firma | Devuelve |
|---|---|---|
| `attach` | `bool attach(RasterGradientRange range, Span<const u16> keys, bool cyclic = true)` | `false` si no hay claves. |
| `set_phase` | `void set_phase(u16 phase)` | — (anima el muestreo). |
| `frame` | `void frame(Scene&)` | — (aporta las intenciones al plan). |
| `bind`/`patch` | `void bind(Scene&)` / `void patch(Scene&) const` | — (materializa una vez + anima parcheando, coste ~0). |

## `Rotozoom`

Estado (ángulo/zoom/offset) + render de una textura indexada a un `ChunkyBuffer` (para C2P). Es un
`RenderEffect`. `configure(Rotozoom)`, `set_angle(phase)`, `render<TW,TH>(IndexedTexture, ChunkyBuffer, w, h)`.

## `SpriteLayer`

**Sprite-as-playfield**: canales de sprite rearmados por línea para cubrir la pantalla **sin bitplanes**
(capa de fondo). `attach(Config)`, `set_scroll(x)`, `emit_into(sched)`/`frame(scene)`,
`bind(sched)`/`patch(words)` (parcheo por frame a coste ~0). Config: `{first_line, lines, channels,
hpos0, hpos_step, dma_channels, dma_data, dma_stride, …}`.

## `FineScroll`

Scroll horizontal fino de **una** capa planar (guarda + columna entrante). `attach(Config)`,
`step()` → `true` al cruzar word (toca shift + columna), `bplcon1()`, `column()`, `shift_job()`,
`column_job(const u16* col)`, `ddfstrt()`.

> Fuente: `engine/include/eng/api/effects.hpp`. Técnicas: `docs/reference/amiga/techniques/`.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
