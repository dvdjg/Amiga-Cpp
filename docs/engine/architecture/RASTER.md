# Rasterizado: CPU y Blitter tras una interfaz uniforme

`field::Surface` es el **contexto de dispositivo** del engine: expone una API de dibujo
uniforme (`set_pixel`, `draw_line`, `fill_rect`, `fill_polygon`, `blit`, `blit_masked`,
`draw_text`) sin que el consumidor sepa si detrás hay **CPU** o **Blitter**. Este documento
describe el *seam* `field::Rasterizer` (`engine/include/eng/field/raster.hpp`) que hace
transparente esa elección.

```
   Surface  (API estable)
      │  fill_rect / blit / blit_masked
      ▼
   Rasterizer  (seam)
   ├── CpuRaster      → Playfield::draw_span_op (RasterOp) / copy_rect_cpu
   └── BlitterRaster  → Playfield::fill_polygon (PolygonFillSink/Blitter) / FramePlan
```

## Tipos

| Tipo | Quién lo define | Qué describe |
|---|---|---|
| `RasterOp` (`Copy`/`Or`/`And`/`Xor`/`Clear`) | engine | Operación lógica de una escritura (CPU: lógica de palabras; Blitter: `minterm`). |
| `RasterCaps` | **backend** | Hay Blitter, ancho de bus (16 OCS / 32-64 AGA), fill/line/shift/minterms, `setup_cycles`. |
| `RasterPolicy` | **app/escena** | `AccelMode::Auto`/`Cpu`/`Blitter`, área mínima para el Blitter, `cpu_fast`. |
| `Rasterizer` | engine | Interfaz: `fill_rect` / `copy_rect` / `copy_masked`. |

## Implementaciones

- **`CpuRaster`**: relleno por scanline (`Playfield::draw_span_op`, con `RasterOp`), **línea**
  (Bresenham recortada al `ClipRect`) y copias por CPU (`Playfield::copy_rect_cpu` /
  `copy_masked_cpu`). La copia usa stores de **32 bits** cuando `RasterPolicy::cpu_fast` está
  activo y origen/destino quedan alineados a 4 (ruta `move.l`; *CPU blit assist* del 68020, ver
  `../guides/optimization/OPTIMIZACION_GPP_68000.md`). Sin multiplicaciones de 32 bits en el
  bucle (`mulu16` + avance de punteros).
- **`BlitterRaster`**: relleno por `Playfield::fill_polygon` (usa el `PolygonFillSink`/Blitter si
  está instalado; si no, CPU) y copias por el `FramePlan` (`CopyRect`/`MaskedBobCookieCut`, con
  `source_shift` y `descending`); **línea** por CPU (el `BlitJob` de línea por Blitter está
  pendiente).

## Selección (backend → escena)

El backend declara sus capacidades y ofrece un atajo para instalar el rasterizador; la escena
no conoce al backend:

```cpp
// amiga_minimal (OCS/AGA): Blitter de 16 bits, fill/line/shift/minterms
backend.install_raster(scene);   // elige kBlitterRaster/kCpuRaster segun raster_caps()
scene.surface().fill_rect(x, y, w, h, color, field::RasterOp::Xor); // misma llamada
```

`Scene::set_raster(rasterizer, policy)` fija la elección; `Surface` la lee del playfield. Un
backend host (sin Blitter) declara `RasterCaps{ .blitter = false }` y se usa `kCpuRaster`.

## Estado y extensión

- **Hecho**: `RasterOp` (CPU), relleno CPU/Blitter (con umbral `Auto`), **línea** en la API
  (`Rasterizer::draw_line`, CPU), copia CPU (32 bits) y Blitter con `source_shift`/`descending`,
  copia enmascarada CPU y Blitter; `install_raster` con `RasterCaps` OCS/AGA por target.
- **Operaciones Blitter que aún NO cubre el seam** (ver `frame_plan.hpp`/AHRM cap. 6):
  - **Línea por Blitter** (`BLTCON1` LINE): falta un `BlitJobKind::Line` (con sus coordenadas) y
    que `BlitterRaster::draw_line` lo encole; hoy la línea es CPU.
  - **Relleno de rect directo** (FILL mode): hoy el rect se rellena como polígono vía
    `PolygonFillSink`; un `FillRect` propio evitaría el camino de polígono.
  - **C2P** (chunky→planar): uso especializado multi-fase del Blitter (ver `C2P_BLITTER.md`).
  - **Copia CPU siempre de 32 bits**: hoy el camino ancho requiere origen y destino alineados a
    4; con `row_bytes % 4 == 0` (p. ej. 320 px) aplica, si no cae a 16 bits por fila. Alinear
    los buffers en `bind` (o padear `row_bytes` a 4) lo haría universal.

## Verificación

- **HOST-212**: `RasterOp` (`Xor` dos veces = 0, `Or`/`And`/`Clear`), `BlitterRaster` (fill y
  copia por `FramePlan`) y copia CPU/enmascarada CPU (pixeles + `blit_job_count`).
- **Sonda de codegen** `docs/guides/optimization/_probe_raster_copy.cpp`: 68000/68020 sin
  libcalls (`__mulsi3`/`__udivsi3`) y con `move.l` en la copia.
- **Demo**: `077_math3d_cube` instala el rasterizador del backend y dibuja con `Surface`.

Referencias: `SCENE_COMPOSITION.md` §6.2, `DISPLAY_COMPOSITION.md`, `playfield.hpp`
(`PolygonFillSink`), `frame_plan.hpp` (`BlitJob`/`minterm`).
