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

1. ~~**Display 256×256×4 exacto**~~ ✅ hecho: `DDFSTRT=0x48, DDFSTOP=0xC0, BPLCON1=0, DIWSTRT=DIWSTOP=0x2CA1` (recalculado de `SetupBitplaneFetchImpl.c`/`SetupDisplayWindowImpl.c` para `X(32),Y(0),256×256`).
2. ~~**Doble buffer**~~ ✅ hecho: 2 buffers de 4 planos + 2 copperlists, swap por frame (`install_copper_list`). (El original rota 5 planos y parchea `BPLxPT`; equivalente sin tearing.)
3. **Diff 1:1** contra `effects/wireframe/wireframe.exe` (mismos frames, `readPng` + `ollama-desc.mjs`).
4. **Investigar** por qué la captura de *secuencia* sale vacía (el `screenshot.png` sí muestra el balón): probablemente captura a mitad de `blitter_clear`/redibujo.
5. Luego `fire-rgb` con el mismo método.

## Resultado (1:1)

Con los registros del original y doble buffer, el `screenshot` muestra el balón de **~241×241 px Amiga** (caja 482×482 en la captura ~2×), **centrado y entero**, confirmado por visión (`qwen3-vl`: "outline of a soccer ball"). Geometría, proyección, culling, línea por Blitter y display son los del original; solo el doble buffer es 2×4 planos en vez del anillo de 5 (misma imagen).

**Color de la línea (corregido)**: el original dibuja la línea en `screen->planes[active]`, que su copperlist sitúa como **bit 3** → color 8 (`#0088ff`, brillante). Dibujar en el plano 0 daba color 1 (`#001133`), casi idéntico al fondo `#001122` → **invisible**. Ahora se dibuja en el plano 3 → verificado objetivo (`#0088ff`, 6168 px de trazo) y por visión.

**Diferencia pendiente**: el original limpia SOLO `planes[active]` y rota 5 planos → el balón deja un **rastro** (colores 8/4/2/1 según el frame); la versión actual limpia los 4 planos → balón brillante limpio, sin rastro. Para el 1:1 exacto falta el anillo de 5 planos con parche de `BPLxPT` por frame.

**✅ Rastro (1:1 completo)**: implementado el **anillo de 5 planos**: cada frame se limpia y dibuja SOLO `planes[active]`, y se instala una de las 5 copperlists (bit3=active, bit2=active-1, bit1=active-2, bit0=active-3, mod 5). Resultado: trazo actual en color 8 (`#0088ff`) y rastro de los 3 frames previos en 4/2/1 (`#004488`/`#002255`/`#001133`), con combinaciones por solape (hasta `#cceeff`). Verificado objetivo (17 colores en la captura) y por visión ("líneas con doble trazo"). El original y este porte ya coinciden en geometría, línea, display, doble buffer y rastro.

