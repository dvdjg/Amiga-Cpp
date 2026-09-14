# Plan de importe — `effects/flatshade-convex`

`flatshade-convex.c` dibuja un objeto **convexo** `pilka` (malla `obj2c`) girando con **sombreado plano**: calcula la visibilidad y la **luz por cara**, la visibilidad de **aristas** del sólido convexo (XOR), dibuja esas aristas por **Blitter** (line mode `ONEDOT`+`EOR`) y rellena el hueco con **area fill XOR**. Display 256×256×4, doble buffer, paleta `flatshade-pal.c`. Método: `docs/demos/effects/DEMOSCENE_EFFECT_REPLICATION_POLICY.md`.

## La rebanada (alcance del importe)

```
effects/flatshade-convex/flatshade-convex.c
effects/flatshade-convex/data/{pilka.c, flatshade-pal.c}   (generados por obj2c)
include/{effect.h, blitter.h, copper.h, 3d.h, fx.h, strings.h, sort.h, gfx.h, pixmap.h, ...}
lib/lib3d/*      (Object3D, UpdateObjectTransformation, UpdateFaceVisibility)
lib/libblit/*    (BlitterLine/BlitterClear)  + el line/fill inline del efecto
lib/libgfx/*     (Bitmap, CopList, playfield, paleta, DMA)
system/*         (arranque, bucle de efecto, vblank)
```

## Hot vs fronteras

- **Hot (portar verbatim)**: `UpdateFaceVisibility` (dot + luz con `InvSqrt[512]`), `UpdateEdgeVisibilityConvex` (XOR de la luz en las aristas), `TransformVertices` (`MULVERTEX1/2`, `>>4`, `normfx`, `div16`), `DrawObject` (line mode `ONEDOT`+`EOR` por plano), `BitmapFillFast` (area fill `XOR`).
- **Fronteras (re-expresar)**: `NewBitmap`/`DeleteBitmap`, `SetupPlayfield`/`LoadColors`/`NewCopList`/`CopSetupBitplanes`/`Activate`/`EnableDMA`, `TaskWaitVBlank`, `EFFECT` main loop, `BitmapClearFast`.
- **Sub-píxel**: el efecto original NO uses sub-píxel en las líneas (coordenadas enteras tras `div16`); para la técnica sub-píxel (contorno `ONEDOT` + fill) ver `docs/reference/amiga/techniques/blitter-line-subpixel-fill.md`.

## Tabla de mapeo (demoscene → engine)

| Demoscene | Engine | Estado |
|---|---|---|
| `fx.h` (`sintab`, `normfx`, `fx4i`, `div16`), `3d.h` (`Point3D`/`Matrix3D`), `LoadRotate3D`/`Compose3D`/`Transform3D` | `math2d`/`math3d` (`Mat3x3`, `load_rotate`, `compose`, `transform`, `normfx`, `div16`) | ✅ (HOST-010/011) |
| `Mesh3D`/`Object3D` + macros `NODE3D/POINT/VERTEX/EDGE/FACE` (`obj2c`) | `eng::object3d` (`object3d.hpp`) | ✅ (HOST-014) |
| `NewObject3D` | `obj::new_object3d` (sin alloc) | ✅ |
| `UpdateObjectTransformation` | `object3d::update_object_transformation` | ✅ |
| `UpdateFaceVisibility` (dot + luz `InvSqrt`) | `update_face_visibility` (demo-local, fiel) | ✅ |
| `UpdateEdgeVisibilityConvex` (XOR) | `update_edge_visibility_convex` (demo-local, fiel) | ✅ |
| `TransformVertices` (`MULVERTEX1/2`) | `transform_vertices` (demo-local, verbatim) | ✅ |
| `DrawObject` (`BC0F_LINE_EOR`, `ONEDOT`, `bltapt=derr`, `bltsize=(dmax<<6)+66`, por plano) | **`MinimalBackend::blitter_line_eor`** | ✅ |
| `BitmapFillFast` (`BLITREVERSE|FILL_XOR`, seed = última word) | **`MinimalBackend::blitter_area_fill`** | ✅ |
| `BitmapClearFast` | `MinimalBackend::blitter_clear` | ✅ |
| `SetupPlayfield`/`LoadColors`/`CopSetupBitplanes`/parche `BPLxPT`/`EnableDMA(BLITHOG)` | `copper::Scheduler::emit_planes_display` + `emit_palette` + swap de copperlist | 🔶 equivalente (doble buffer 2×4 en vez de rotación de planos) |
| `EFFECT` main loop / `TaskWaitVBlank` / `frameCount` | `Engine::run_frames_polling` + `g_eng_run_status` + VBlank del compositor | ✅ |

## Qué falta (1:1 exacto)

1. **Display y doble buffer exactos**: el original usa 2 bitmaps de 256×256×4 y parchea `BPLxPT` por frame; aquí son 2 buffers de 4 planos + 2 copperlists (misma imagen, sin tearing).
2. **Comparación frame a frame** (misma fase de rotación) contra `flatshade-convex.adf` con `readPng` + `ollama-desc.mjs`; hoy se comparan frames de fases distintas (stats agregadas).
3. **Rango de luz**: el original alcanza el índice 15 (`#cceeff`) en las caras más iluminadas; la réplica llega a 12 (`#77bbff`). Revisar redondeo de la normalización `InvSqrt` (o el `swap16`/`hi16`) para igualar la escala.
4. **`Import` del `.adf` de referencia** como fixture de comparación (boot floppy).

## Estado del importe (hecho)

- ✅ Modelo `obj2c` + `Object3D` (HOST-014), `update_object_transformation`, `update_face_visibility` (luz con `InvSqrt`), `update_edge_visibility_convex` (XOR), `transform_vertices` (verbatim).
- ✅ Backend: `blitter_line_eor` (line mode `ONEDOT`+`EOR`, secuencia exacta de `DrawObject`) y `blitter_area_fill` (area fill `XOR`, port de `BitmapFillFast`).
- ✅ Demo **`116_flatshade_convex`**: `pilka.c`/paleta del original, display 256×256×4, doble buffer; READY y `verify-116`.
- ✅ Comparación con el original: mismas stats agregadas (cobertura 39.7 % vs 38.9 %, caja ~498 px, fondo `#001122`) y misma descripción por visión (Ollama): poliedro flat-shaded azul, caras sólidas, fondo negro, aristas por contraste.

## Resultado

El balón convexo se dibuja con la técnica exacta del original (aristas convexas por XOR + area fill XOR), a 256×256×4 con su paleta. La captura del original se obtuvo arrancando `flatshade-convex.adf` en WinUAE (el `.exe` suelto no arranca fuera de su floppy). Desviación menor pendiente: el rango de luz (índice 15 vs 12) y la comparación frame a frame.
