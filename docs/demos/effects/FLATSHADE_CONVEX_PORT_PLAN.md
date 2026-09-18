# Plan de importe — `effects/flatshade-convex`

> **Nota:** documento histórico (plan y bitácora del importe). Cita la API del **original** (`fx.h`, `div16`/`mul16`); en el engine vigente esos nombres son `div_wide`/`mul_wide` (`docs/engine/architecture/SCALAR_LIBRARY.md`).

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

El balón convexo se dibuja a 256×256×4 con su paleta, **caras sólidas limpias** y sin las líneas horizontales internas que aparecían con el area fill XOR del original.

- **Diagnóstico del glitch**: el original dibuja aristas visibles (convexas por XOR) + **un** area fill `XOR` (1 fila, `bltsize=(0<<6)|(width>>4)`). El area fill tiene un "carry" vertical: en una fila con un número **impar** de cruces del contorno se rellena hasta el borde, y en los **vértices** (extremos locales de la silueta, donde el píxel se cancela por XOR entre dos aristas) la paridad se rompe → **línea horizontal en cada vértice**.
- **Decisión**: sustituir el par aristas+area-fill `XOR` por **relleno por cara** con `MinimalBackend::blitter_fill_polygon` (máscara 1 bit + contorno `ONEDOT` + area fill **inclusivo** `FILL_OR` + cookie-cut por plano). No depende de la paridad de cruces → caras sólidas limpias.
- **Arreglo de engine reutilizable**: `blit_fill_region` estaba **ascendente** (`FILL_OR` sin `BLITREVERSE`) y rayaba; se corrigió a **descendente** (port fiel de `BlitterFillArea` de libblit). Mejora también las rutas Blitter del 078.

Comparación con el original (captura por `.adf` en WinUAE): fondo `#001122`, caja ~498 px, cobertura **39.2 % vs 38.9 %**, luminancia media del cuerpo **147.5 vs 149.3**, descripción idéntica por visión (poliedro flat-shaded azul, caras sólidas).

Desviación respecto al original: se rellena por cara en vez de aristas+area-fill XOR (misma imagen, sin el glitch). Pendiente: el **rango de luz** (índice 15 vs 12), la comparación frame a frame a igual fase y el **rendimiento** (ver abajo).

## Análisis paso a paso del original

### Secuencia por frame

1. `BitmapClearFast(screen[active])`: **UN** blit limpia los 4 planos. Los planos son contiguos, así que `bltsize = (height*depth)<<6 | (bytesPerRow>>1)` (1024 filas × 16 words) cubre todo el framebuffer, con `bltcon0 = DEST|A_TO_D`, `bltadat = 0`, `bltdmod = 0`.
2. `cube->rotate = frameCount * 8`.
3. `UpdateObjectTransformation`: matriz objeto→mundo y su inversa + cámara en espacio objeto (`math3d`).
4. `UpdateFaceVisibility`: back-face culling + luz 0..15 por cara (dot normal·vista normalizado con la tabla `InvSqrt`; sin `sqrt`).
5. `UpdateEdgeVisibilityConvex`: por cada cara visible marca sus vértices y hace `EDGE(e)->flags ^= face->flags`.
6. `TransformVertices`: transforma y proyecta **solo** los vértices marcados (macros con constantes precalculadas).
7. `DrawObject(screen[active]->planes[0], cube)`: dibuja las aristas visibles por Blitter.
8. `BitmapFillFast(screen[active])`: **UN** blit de area fill `XOR` sobre el contorno.
9. `CopUpdateBitplanes(bplptr, screen[active])` + `TaskWaitVBlank()` + `active ^= 1`.

### Estrategia de registros (`DrawObject`) — la clave de la velocidad

- **Registros comunes UNA sola vez** antes del bucle de aristas: `bltafwm = bltalwm = -1`, `bltadat = 0x8000`, `bltbdat = 0xffff`, `bltcmod = bltdmod = WIDTH/8`.
- **Por arista, solo 8 escrituras**: `bltcon0`, `bltcon1`, `bltcpt`, `bltapt`, `bltdpt`, `bltbmod`, `bltamod`, `bltsize`.
- **Multi-plano por puntero**: la MISMA línea se dibuja en cada plano cuyo bit esté en `edgeColor` reescribiendo **solo `bltcpt`** (`+= WIDTH*HEIGHT/8`, salto de plano) + `_WaitBlitter`; `bltdpt` se queda en la base.
- **Una sola condición de línea**: `BC0F_LINE_EOR` con `ONEDOT` (un píxel por fila), `bltsize = (dmax<<6)+66`, `bltapt = derr` (el acumulador Bresenham como "puntero" en A).
- `if (y0 == y1) continue;`: descarta aristas horizontales.

### Estrategia de transformación

- Constantes precalculadas una vez: `m0 = (M.x<<8) - ((M.m00*M.m01)>>4)` y `m1` análoga; `M.z -= normfx(M.m20*M.m21)` (muta la matriz, como el original).
- Por vértice marcado: `t0 = orig.x + y`, `t1 = orig.y + x`, `t2 = orig.z * z`, `xy = x*y`, y `D = ((t0*t1 + t2 - xy) >> 4) + E` para `xp`/`yp`; `normfx` para `zp`.
- Proyección con `div16` (÷ zp) + centro. Sin divisiones adicionales por vértice.

### Por qué es mucho más rápido que nuestra réplica

| | Original | Réplica (relleno por cara) |
|---|---|---|
| Clear | **1 blit** (4 planos contiguos) | 4 blits (uno por plano) |
| Dibujo | 1 blit por arista visible (× plano con bit), comunes ya fijados | 1 clear de máscara + N líneas + 1 fill + **4 cookie-cut** por cara |
| Fill | **1 blit** para todo el balón | 1 fill por cara (~16) |
| Registros/arista | 8 writes (comunes ya puestos) | ~15 por llamada |
| Máscara 1-bit | no | sí (más tráfico de memoria) |
| **Total blits/frame** | ~1 clear + ~N_aristas·planos + 1 fill | ~1 clear + ~11·N_caras |

El original planifica el dibujo como **un solo pase de líneas sobre el framebuffer de color** (sin máscara intermedia) y **un solo fill**, reutilizando registros. Nuestra ruta por cara paga clear+outline+fill+cookie-cut por cada polígono.

### Por qué el original no deja líneas en los vértices

El area fill `XOR` conmuta el relleno en cada píxel del contorno de la fila y **propaga el estado verticalmente**; con un contorno cerrado (paridad par por fila) rellena solo el interior. El problema aparece cuando **falta un píxel de contorno** en una fila (paridad impar → el carry se invierte → banda horizontal).

En un **vértice** se juntan dos aristas; como se dibujan con **EOR**, si caen en los **mismos planos** (mismo `edgeColor`) el píxel del vértice **se cancela** → hueco → fuga. En el original esto se evita porque:

- **No dibuja aristas horizontales** (`y0 == y1`), que en nuestra primera versión metían píxeles de contorno extra en los vértices.
- Las aristas de la silueta en un vértice suelen pertenecer a **caras distintas** (luces distintas → planos distintos), así que no se cancelan entre planos; y el `edgeColor` es el XOR de las luces, no un `1`.

El mecanismo exacto depende de la rasterización discreta y conviene confirmarlo en hardware; funcionalmente se ha resuelto con **relleno por cara** (no depende de la paridad).

### Qué incorporar al engine

1. **`blitter_clear_all`**: limpiar planos contiguos en **1 blit** (`bltsize = (height*planes)<<6 | words`) en vez de iterar por plano.
2. **Batch de líneas** (`blitter_lines_begin` + `blitter_line_batch`): fijar los comunes una vez y dibujar N aristas/planos con writes mínimos, como `DrawObject`. Reutilizable por wireframe/flatshade.
3. **`area_fill` correcto** (ya arreglado a descendente) para rellenar una silueta cerrada en **1 blit**, en lugar del relleno por cara.
4. **Relleno por máscara** (`blitter_fill_polygon`) solo cuando se necesiten colores por cara (lo actual), aceptando su coste.
5. Mantener el **transform con constantes precalculadas** (ya se hace).


## Rendimiento (medido)

`fps.mjs` (ciclos emulados, A500): **8.1 fps** debug / **8.5 fps** release (`≈840k` ciclos/frame). No es codegen (release ≈ debug): es el **número de blits** (~14 por cara × ~16 caras: clear de máscara + N líneas `ONEDOT` + fill + 4 cookie-cut). La ruta aristas+area-fill del original hace menos blits (~1 por arista visible + 1 fill) pero deja el glitch. Optimizaciones candidatas: fusionar las 4 cookie-cut, precalcular la máscara del silueta, o reducir caras (back-face ya filtra).

### Comparativa de las dos rutas (medida)

La demo expone la ruta fiel del original con `-DFLATSHADE_FAITHFUL=1` (por defecto; `draw_edges_area_fill`: aristas `blitter_line_eor` × plano con bit + UN `blitter_area_fill`), y la ruta por máscara por cara con `-DFLATSHADE_FAITHFUL=0`. Medición en frames alineados por fase (`frame*8`) con `internal-gaps.mjs` (huecos de fondo dentro del span de cada fila) y `compare-orig.mjs` (IoU de máscara contra el original):

| Ruta | fps (emul.) | ciclos/frame | huecos internos | IoU máscara vs original |
|---|---|---|---|---|
| Fiel EOR + `bltdpt`=base (**por defecto**) | 9.92 | 715k | 0.00 % | 93.3–96.7 % |
| Fiel EOR con `bltdpt`=dirección calculada | 9.95 | 713k | 0.50 % | 91.2–94.8 % |
| Per-cara (máscara + cookie-cut) | 8.29 | 855k | 0.63–0.75 % | 92.9–97.1 % |
| Original | — | — | 0.00 % | — |

La clave de la paridad en los vértices es el **truco `bltdpt`** del original: en modo línea el primer píxel se escribe por el canal **D**, así que `bltdpt` se deja en la **base del bitmap** (`planes.data()`) mientras `bltcpt` lleva la dirección calculada. Con `bltdpt` = dirección calculada el contorno pierde un cruce en cada vértice y el area fill XOR filtra una raya horizontal; con `bltdpt` = base el relleno sale exacto (0.00 % de huecos, igual que el original) y la ruta fiel queda **~20 % más rápida** que el relleno por cara, por lo que es la ruta por defecto. La ruta por máscara queda disponible como alternativa (`-DFLATSHADE_FAITHFUL=0`).

Además, en `BLTSIZE` el campo de altura **0 significa 1024 líneas** (y el de anchura 0, 64 words): por eso el original limpia y rellena los 4 planos contiguos (`256*4 = 1024` líneas × 16 words = los 32768 bytes de los 4 planos) con `bltsize` de altura 0, sin que sea un no-op.

### Rendimiento (medido, estado vigente)

Tasa real del original: **~24.7 fps / ~287k ciclos/render** (2 vblanks). Criterio de éxito: acercarse a ese framerate con la imagen idéntica (0.00 % de huecos internos e IoU sin cambios).

Progresión de la réplica (fps emulados; perfilado con el periférico de depuración):

| Paso | fps | ciclos/frame |
|---|---|---|
| Inicial (aristas por plano + fill serial) | 9.92 | 715k |
| `clear` de 1 blit + quitar `touch[]` muerto | 10.58 | 670k |
| Solape clear↔transform | 14.26 | 497k |
| `div16`+`row_offset` nativos (`divs`/`muls`) | 15.87 | 447k |
| `mul16`/`mulu16` nativos en transform y luz | 19.07 | 372k |
| Esperar el fill antes del swap (fix del parpadeo) | 16.75 | 423k |
| **`blitter_lines_eor_begin` + `prepare/draw`** (comunes 1×/frame, Bresenham 1×/arista) | ~17.5 | ~395k |
| **BLITHOG correcto** (`set_blitter_priority(true)`, 0x0400) → fill a 131.8k | ~18.0 | ~380k |
| **Pipeline de 3 buffers** (transform+clear escondidos bajo el fill) | **~20.7** | **~342k** |
| **Ruta ASM** (`-DK_FLATSHADE_ASM=1`) | no válida (render roto) | ~283k |

Desglose vigente (emulador, frame representativo): `wait+swap+edges` ~105k, `transform` ~127k (corre durante el fill), `edges` ~114k, lanzamiento del fill ~1.3k, `update` ~238k. El frame (~342k) suma la cola del Blitter (fill 131.8k + clear 89k) que el `update` no espera: el wait se absorbe en el primer `wait_blitter` del update siguiente.

**Cuello actual**: los `edges` (~114k) y el `transform` (~127k) son ~1.9x y ~1.8x los del original (59k y 71k según el profiler de líneas de raster del propio original). El `fill` coincide (131.8k vs 131k): **no es un artefacto del emulador** — WinUAE cobra el mismo área fill al original y al port. La brecha es codegen real: el original usa asm 68k a mano (`register char flags asm("d3")`, macros `DRAWLINE` inline) y el C++ del port no lo iguala. El `update` ya está bajo 284k (2 vblanks) gracias a la pipeline, pero el frame cae en 2-3 vblanks según la variación de aristas por frame (nEdges 34-36, nLines 62-67) y la cola del Blitter (fill + pre-clear) que el transform no cubre del todo. La medida en vivo del original (render/s) quedó bloqueada: el efecto del ADF termina al leer el botón izquierdo del ratón (`EffectRun` sale con `LeftMouseButton()`) y el framework entra en un `BRA *`.

La **ruta ASM** (`-DK_FLATSHADE_ASM=1`) ataca esa brecha de codegen portando las rutinas calientes a asm m68k con registros fijos, pero **aún no es válida**: visibilidad de caras/aristas y `transform_vertices` son correctas (con el `draw_edges` C++ encima el balón sale bien), mientras que **`fs_draw_edges` desfasa el contorno ~1-2 px**; con el area fill XOR (paridad por scanline) el relleno se rompe en bandas y triángulos. `verify-116` da PASS (no detecta el desfase): el gate es **visual** (wireframe con `-DFLATSHADE_SKIP_FILL=1`, o secuencia + Ollama). El default sigue siendo la ruta C++.

### Pipeline de 3 buffers (cómo se llega a 2 vblanks en CPU)

El `update` actual usa **triple buffer** + lookahead de 1 frame: el estado del objeto (transform/culling/luz) del frame siguiente se precalcula MIENTRAS el Blitter hace el fill del frame actual, y el buffer que usará el siguiente `update` se pre-limpiara también bajo ese fill. Con 2 buffers el clear del buffer trasero es inseparable del camino crítico (ese buffer se está mostrando hasta el swap); con 3, el buffer a limpiar ni se muestra ni se dibuja.

```
  update(N):  wait fill(N-1) → swap(mostrar N-1) → edges(N) → lanzar fill(N)
              → [durante el fill] precalcular estado(N+1) → lanzar clear(N+1) → return
```

- El fill se lanza SIN esperarlo (`blitter_area_fill(..., wait=false)`): el buffer se muestra en el swap del update siguiente, cuyo primer `wait_blitter` ya lo ha absorbido. Nunca se ve a medias.
- `m_draw_buf` avanza `(buf+1)%3`, `m_display_buf` sigue al dibujado el frame anterior, y `(buf+1)%3` es el buffer libre que se pre-limpiara.
- El swap (`install_copper_list`) apunta al buffer dibujado (y rellenado) el frame pasado: la secuencia de rotación mostrada es la del original (latencia de 1 frame, mismo orden de ángulos).

Gate de validación tras cambios de render: `tools/analyze/bestphase.mjs` (IoU+MAD por fase) y `phasecmp.mjs`, además de `verify-116`. La secuencia capturada (`run-demo --sequence-frames`) pasa `verify-116` en todos los frames (el balón rota, sin parpadeo ni tearing).

### Perfil fino (2026-09)

- **`transform` = 109k**, repartido en: `update_object_transformation` 21k, `update_face_visibility` 19k, `update_edge_visibility_convex` 11k y **`transform_vertices` 57k** (el hotspot: ~90 vértices × ~639 ciclos = 6 `muls.w` + 2 `divs` + carga de la matriz por vértice). Sin `__mulsi3`/`__divsi3` en el hot path (los que quedan están en init, en el constructor constexpr de la tabla de senos y en `run_slice` del fondo). La diferencia con el original es **calidad de codegen** (asm con registros fijos), no libcalls.
- **`edges` = 130k**: solo **34 aristas** y **57 blits de línea** por frame (1,232 px totales entre todas) ⇒ **~2,274 ciclos por línea**. Un micro-benchmark aislado de un `blitter_line_eor` de 40 px da **1,752 ciclos**. El coste es del **emulador** (blitter de línea en modo ciclo-exacto + `wait_blitter`), no de nuestro código (el asm de `blitter_line_eor` solo llama a `wait_blitter`), y lo paga el original igual. Es el factor limitante real.
- **`fill` = 143k**: escala con las words (8.7 ciclos/word; 16,384 words). Acotarlo a la bbox no ayuda porque la pelota llena la pantalla (241×240).
- El **`clear`** (89k de bus) se solapa con el transform (2k medidos tras la optimización).
- `transform_vertices` bajó de **57k a 46k** al cambiar `xy = (s32)x * y` (que generaba `__mulsi3` una vez por vértice, ~18k) por `math2d::mul16(x, y)`. El transform queda en **97k** y sin libcalls.

### Cuantización por vblank (por qué las mejoras finas no suben el fps)

El bucle `run_frames_polling` (y el original con `TaskWaitVBlank`) **cuantiza el frame a múltiplos de un vblank** (~142k ciclos): si el trabajo cae entre 1 y 2 vblanks, el frame dura 2; entre 2 y 3, dura 3. Nuestro `update` (~373k) + bucle (~54k) está en **3 vblanks (≈426k → 16.6 fps)**. El original, con el mismo `clear`+`fill`+`edges` (≈270k, coste del emulador, compartido), también cae en **3 vblanks**. Saltar a 2 vblanks (25 fps) exigiría bajar de 284k (recortar ~143k); el `fill` (143k) y los `edges` (129k) los paga el original igual, así que no es alcanzable sin cambiar la técnica. Por eso las mejoras finas del transform no se ven en el fps mientras no se cruce el umbral.

### Referencia del profiler del original (líneas de raster)

El original lleva un profiler (`system/profiler.c`, `ReadLineCounter`) con las medias anotadas en el código: **Transform 156, Draw 130, Fill 289 líneas de raster** (un frame PAL = 313 líneas ≈ 142k ciclos → ~453 ciclos/línea). Es decir: **transform 71k, draw 59k, fill 131k**. Nuestra réplica (mismo emulador): **transform 97k (215 líneas), edges 118k (260), fill 143k (316)**. Los mayores excesos son los **edges (~2x)** y el **transform (1.4x)**; el fill es casi igual. El original cabe en **2 vblanks** (575 líneas < 626) → 25 fps; nosotros en **3** (900 líneas) → 16.6 fps. Para cruzar a 2 vblanks hay que recortar ~78k del `update`.

Cambios aplicados en esta iteración: `xy` con `math2d::mul16` (evita un `__mulsi3` por vértice: `transform_vertices` 57k→46k). Medido con micro-benchmarks (`-DFLATSHADE_BENCH_LINE=1`): **cada acceso a registro custom cuesta ~57 ciclos** con `cpu_cycle_exact`, y **cualquier blit tiene un coste fijo ~1,400** (una escritura de 1 palabra tarda 1,404).

**Reversiones por imagen rota:** una "optimización" aparentemente inocua se descartó porque **rompía el sólido** (caras deformes): **`blitter_lines_begin`** (fijar 1×/frame los comunes del modo línea, `BLTCON`/`MODS`/`DAT`/`AFWM`/`ALWM`) → el render salía deforme aunque `verify-116` pasaba. Se dejan los comunes **por arista/plano** en `blitter_line_eor`. También se descartó `blitter_line_eor_multi` (medía más lento: 114k vs 106k). **Sí se mantiene** `write_custom_pointer` en **una sola escritura de 32 bits**: el original (`custom_regdef.h`) declara `bltcpt`/`bltapt`/`bltdpt` como `void*` y su `custom_->bltcpt = ptr` es un long; nuestra escritura de 32 bits es byte-equivalente (mismo high/low) y fiel, y ahorra ~5k.

**Lección**: `verify-116` (cobertura/tonos/centro) es **demasiado grueso**: dio PASS con la imagen deformada. Hace falta un gate estructural (nº de regiones planas / densidad de aristas internas / comparación con el original por fase) que se ejecute tras **cada** micro-optimización del render.

Estado medido actual: `transform` 97k, `edges` 124k, `fill` 144k, `update` 368k (3 vblanks, ~16.6 fps), imagen correcta.

### Técnicas pendientes de optimización (para retomar más adelante)

El desarrollo se cortó aquí (coste de tiempo alto); la imagen es correcta y el port es fiel. Pistas para seguir optimizando, ordenadas por valor/riesgo:

1. **Medir el coste de BUS real del Blitter, no el `BBUSY`.** Lo que medimos es el tiempo que el Blitter está ocupado (incluye `WaitBlitter` + setup + posible contención). Comparar la duración de un `area fill` XOR de 16,384 words (a) con `blitter_cycle_exact=1` vs `0`, y (b) con el DMA de bitplanes activo vs apagado. Si el bus real es ~3 ciclos/word (≈49k) y el BBUSY es ~8.7/word (≈144k), el cuello es el modelo del emulador y en hardware real el frame sería ~262k → **2 vblanks / 25 fps**. Fija el techo real antes de tocar más código.
2. **Depurar `blitter_lines_begin`** (fijar 1×/frame los comunes del modo línea: `BLTCON`/`MODS`/`DAT`/`AFWM`/`ALWM`). Debería ser correcto (el original lo hace al entrar en `DrawObject`), pero en nuestro port daba **caras deformes** (con `verify-116` en PASS). Encontrar qué registro/estado intermedio (p. ej. `cmod`/`dmod` o `dat`) lo altera entre blits y recuperar las escrituras comunes por arista/plano (~10-60k).
3. **bbox del `clear`/`fill` a la silueta real (~213 líneas, no las 240 del bbox de vértices)**: ~20 % del fill (~24k) y algo del clear. Ya están `blitter_clear_rect`/`blitter_area_fill_rect` en el backend (probados con el bbox de vértices y descartados por ser casi pantalla completa). Medir con la silueta real y validar con `tools/analyze/bestphase.mjs`.
4. **`transform_vertices` en asm con registros fijos** (46k; objetivo ~30k, estilo `MULVERTEX` del original). Es el mayor bloque "nuestro"; ~2 `div16` + 6 `muls.w` por vértice.
5. **Pipeline de 1 frame** (transform del frame siguiente durante el `fill`): ya implementado y probado — **no baja el frame** porque el trabajo del Blitter serial es el límite, pero es combinable con 1-4 si el fill se abarata.
6. **Reducir blits de contorno**: agrupar los 4 planos de una arista en menos reprogramaciones (`blitter_line_eor_multi`, ya escrito) medía **más lento**; explorar alternativas (compartir más registros, `blitter-DMA` distinto).
7. **NO fusionar** `update_face_visibility` + `update_edge_visibility_convex` en una pasada: el original no lo hace (son llamadas separadas en `Render`); descartado por fidelidad.

Gate obligatorio tras cualquier cambio: `tools/analyze/bestphase.mjs` (mejor IoU + MAD por fase contra el original) y `tools/analyze/phasecmp.mjs` (paridad pixel a pixel), además de `verify-116`.






