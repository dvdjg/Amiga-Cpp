# Plan de porte 1:1 — `effects/wireframe`

`wireframe.c` dibuja un objeto `pilka` (malla `obj2c`) en **alambre** con la **línea por Blitter** (modo line OR), doble buffer de 5 planos rotando `BPLxPT`, 256×256×4, y la paleta de `wireframe-pal.c`. Es el patrón ideal para calibrar el porte 1:1 (método: `docs/guides/roadmap/PORT_PROMPT_1A1.md`).

## La rebanada (alcance del porte)

El efecto NO es un fichero; es esta rebanada:

```
effects/wireframe/wireframe.c
effects/wireframe/data/{pilka.c, wireframe-pal.c}   (generados por obj2c)
include/{effect.h, blitter.h, copper.h, 3d.h, fx.h, strings.h, sort.h, gfx.h, pixmap.h, 2d.h, cdefs.h, config.h, debug.h}
lib/lib3d/*      (Object3D, matrices, visibilidad, transform)
lib/libblit/*    (BlitterLine, BlitterClear, WordMask)
lib/libgfx/*     (Bitmap, CopList, playfield, palette, DMA)
lib/libmisc/*    (fx/sintab/div16/isqrt, strings/sort)
system/*         (arranque, bucle de efecto, vblank, allocator)
```

## Hot vs fronteras

- **Hot (portar verbatim)**: `DrawObject` (secuencia de registros del Blitter), `TransformVertices` (macros `MULVERTEX1/2` + `div16`), `UpdateFaceVisibilityFast` / `UpdateEdgeVisibility` (recorrido de `faceGroups` con flags `char` 0/-1), `UpdateObjectTransformation` (matriz directa/inversa + cámara en espacio objeto).
- **Fronteras (re-expresar)**: `NewBitmap`/`DeleteBitmap` (alloc), `SetupPlayfield`/`LoadColors`/`NewCopList`/`CopSetupBitplanes`/`CopListActivate`/`CopInsSet32`/`EnableDMA` (display), `TaskWaitVBlank` (sync), `EFFECT` main loop, `BlitterClear`.

## Tabla de mapeo (demoscene → engine)

| Demoscene | Engine actual | Estado |
|---|---|---|
| `fx.h`: `SIN/COS/sintab`, `normfx`, `shift12`, `fx4i/fx12i`, `div16`, `isqrt` | `math2d`: `sin_q12/cos_q12/SinTableQ12`, `normfx`, `div16`; `isqrt.hpp` | ✅ equivalentes |
| `Point3D`, `Matrix3D` + `LoadRotate3D/LoadReverseRotate3D/Scale3D/Translate3D/Compose3D/Transform3D` | `math3d::Vec3/Mat3x3` + `load_rotate/load_reverse_rotate/scale/translate/compose/transform` | ✅ equivalentes (verificar orden/args de `compose`: `md = ma*mb`) |
| `FaceT`, back-face culling, orden por Z | `math3d::Face`, `face_visible/face_z_sum/face_z_min`; `mesh3d::mesh_painter_order` | 🔶 primitivas ✅; **falta** el recorrido con flags (`char` 0/-1) y `SortFaces`/`visibleFace` |
| `Mesh3D`/`Object3D` + macros `NODE3D/POINT/VERTEX/EDGE/FACE` (formato `obj2c`) | — (nuestro `MeshView` es otro modelo) | ❌ **falta el modelo empaquetado `obj2c`** |
| `NewObject3D`/`DeleteObject3D` | — | ❌ falta (alloc del objeto + array `visibleFace`) |
| `UpdateObjectTransformation` | primitivas en `math3d` | 🔶 falta la función (wrapper sobre `math3d` + `div16`) |
| `UpdateFaceVisibilityFast`/`UpdateVertexVisibility` | culling en `mesh3d` | 🔶 falta la variante con flags |
| `BlitterLine`/`DrawObject` | `MinimalBackend` (CopyRect) + mi `fill_triangles_blitter`/`blit_line` (WIP, basado en `BlitterLine.c` EOR+ONEDOT) | ❌ **falta el `DrawObject` exacto**: `bltcon0=rorw(x0&15,4)|BC0F_LINE_OR`, `bltcon1=LINEMODE|SUD/AUL/SUL|SIGNFLAG|rorw(x0&15,4)`, `bltamod=derr-dmax`, `bltbmod=dmin<<1`, `bltapt=(void*)derr`, `bltsize=(dmax<<6)+66`, sin ONEDOT |
| `BlitterClear` | `FramePlan` `CopyRect` | 🔶 equivalente |
| `BitmapT`/`NewBitmap` (planos separados) | `StaticEhbScene` (fijo 320×256×6 EHB) | 🔶 **falta playfield genérico W/H/planos** |
| `NewCopList`/`CopSetupBitplanes`/`CopInsSet32` (parche `BPLxPT`) | `copper::Scheduler` (`emit_planes_display`, paramétrico) | 🔶 falta el parcheo por frame (swap de buffer) |
| `SetupPlayfield`/`LoadColors`/`EnableDMA(BLITTER|RASTER|BLITHOG)` | `StaticEhbScene`/`emit_palette`/backend | 🔶 falta playfield no-EHB 4 planos 256×256 y revisar `BLITHOG` |
| `effect.h` (loop, `frameCount`, `TaskWaitVBlank`, `Profile`) | `Engine::run_frames` + `g_eng_run_status` + `wait_vblank` | ✅ |
| `data/pilka.c`, `data/wireframe-pal.c` | se incrustan tal cual (incbin/include) | ✅ (requiere lector `obj2c`) |
| `strings.h`, `sort.h` | equivalentes propios / de soporte | ✅ |

## Qué falta, por orden (blockers)

1. **Modelo `obj2c` + `Object3D` (`lib3d`) 1:1** sobre `math3d`: structs `Node3D/EdgeT/FaceT/Mesh3D/Object3D`, macros de offset (`NODE3D/POINT/VERTEX/EDGE/FACE`), `NewObject3D`, `UpdateObjectTransformation`, `UpdateFaceVisibility(Fast)`, `UpdateVertexVisibility`, `Transform3D`. Es el hueco más grande: nuestro `MeshView` (Span de `Vec3`/`Face`) NO es equivalente para el recorrido verbatim; conviene portar el modelo empaquetado, no re-adaptarlo.
2. **`BlitterLine` exacto de `DrawObject`** expuesto por el backend (línea por hardware, OR, `bltapt=derr`). Resuelve de paso la velocidad de 077 (hoy Bresenham CPU).
3. **Playfield genérico** W×H×planos (no el EHB fijo) + **doble buffer por rotación de planos** con parche de `BPLxPT` por frame.
4. **`BlitterClear`** por frame (ya hay `CopyRect`, reutilizar).
5. **Demo `07x_wireframe`** con `pilka.c` + paleta, y comparación 1:1 contra `wireframe.exe` (diff de frames + `ollama-desc.mjs`).

## Respuesta a "si lib3d está importado, no debería haber problema"

Correcto, con un matiz: lo que hay que importar es **`lib3d` tal cual** (modelo `obj2c` + object model), no sustituirlo por `mesh3d`. Nuestras `math3d`/`math2d` ya cubren las primitivas (matrices 4.12, `normfx`, `div16`, `isqrt`), así que los ficheros de `lib3d` quedan como wrappers finos sobre ellas. Con eso + la línea por Blitter literal + un playfield parametrizable y doble-buffer, `wireframe` entra 1:1. La palanca de optimización posterior (comparar con `fire-rgb`) será cambiar **solo las fronteras** y medir dónde cae la calidad/velocidad.

## Estado del porte (hecho)

- ✅ **Modelo `obj2c` + `Object3D`** portado 1:1 en `engine/include/eng/core/object3d.hpp` (`Node3D/Edge/Face/Mesh3D/Object3D`, macros de offset, `new_object3d`, `update_object_transformation` sobre `math2d`/`math3d`). Test **HOST-014**. `div16`/`normfx` del engine coinciden con el original (comprobado).
- ✅ **Línea por Blitter** en `MinimalBackend::blitter_line` (secuencia idéntica a `DrawObject`: `BC0F_LINE_OR`, `bltapt=derr`, `bltsize=(dmax<<6)+66`, sin ONEDOT) + `blitter_clear`.
- ✅ **Demo `079_wireframe`**: `pilka.c`/paleta copiados tal cual, recorrido (`UpdateFaceVisibilityFast`/`UpdateEdgeVisibility`/`TransformVertices`/`DrawObject`) portado verbatim; arranca (READY) y **visión (qwen3-vl) confirma el balón de alambre** centrado. Adaptaciones (versión B): display 320×256×4 del engine y sin doble buffer.

## Pendiente (para 1:1 exacto)

1. **Display 256×256×4 exacto** del original (`SetupPlayfield(MODE_LORES,4,X(32),Y(0),256,256)` → DDFSTRT=0, DDFSTOP=0x78, BPLCON1=0xCC, DIWSTRT/STOP de `SetupDisplayWindow`) en vez del 320×256×4 de la versión B.
2. **Doble buffer**: 5 planos (`DEPTH+1`) con `active` rotando y parcheo de `BPLxPT` por frame (`CopInsSet32`).
3. **Diff 1:1** contra `effects/wireframe/wireframe.exe` (mismos frames, `readPng` + `ollama-desc.mjs`).
4. **Investigar** por qué la captura de *secuencia* sale vacía (el `screenshot.png` sí muestra el balón): probablemente captura a mitad de `blitter_clear`/redibujo.
5. Luego `fire-rgb` con el mismo método.

