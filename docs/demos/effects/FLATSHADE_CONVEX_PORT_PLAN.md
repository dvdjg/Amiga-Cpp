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

### Optimización (objetivo: igualar el framerate del original)

Estado: la ruta por defecto corre a **~9.9 fps** (715k ciclos/frame; debug ≈ release, no es codegen). El original (vsync-locked con doble buffer) marca el objetivo. Plan:

0. **Medir el fps del original** por `.adf`: leer el contador de ciclos emulados (`0xB7E928`) y contar los frames efectivos (avance de `rotate`/`frameCount` o del puntero de buffer activo) para fijar el objetivo real (50 fps si cabe en un frame).
1. **Perfilar 116 con checkpoints del periférico** (`0xB70000`, como la demo 101) alrededor de: `clear`, `update_object_transformation`+visibilidad, contorno (bucle de líneas) y `area fill`. Desglose de ciclos por sección.
2. **Sospechosos a medir/atacar**:
   - `wait_blitter` **por línea** (secuencia serial). El original también espera por llamada de `DRAWLINE`, pero conviene medir si el coste está aquí; se puede programar la siguiente línea sin esperar la anterior cuando tocan planos/regiones distintas (pipelining), dejando solo el último `wait` antes del fill.
   - **Setup común de registros una vez por frame** (`bltafwm/alwm`, `bltadat`, `bltbdat`, `bltcmod/bltdmod`) como el original, en vez de por cada llamada de línea.
   - **`blitter_clear` hace 4 blits** (uno por plano); el original hace **1** (4 planos contiguos, anchura `w/16`, altura 0). Pasar a un solo blit.
   - **Replicación por plano en una sola programación**: el original comparte todo y solo cambia `bltcpt += plane_bytes` por plano; nuestro bucle llama a `blitter_line_eor` una vez por plano (reprograma los mismos registros). Agrupar.
   - **Extensión del fill**: hoy barre los 4 planos completos (`1024×16` words). Si el ahorro lo justifica, rellenar solo la bbox del objeto (el fill even-odd necesita la fila completa del contorno; acotar a la bbox es válido si ninguna arista sale de ella).
3. **Criterio de éxito**: mismo framerate que el original con imagen idéntica (huecos internos 0.00 % e IoU sin cambios). Anotar el resultado en la bitácora de `OPTIMIZACION_GPP_68000.md`.

### Resultados medidos (perfilado)

Tasa real del original (contando cambios del puntero `BPL1PT` en su copperlist, `out/tmp/orig-rate3.mjs`): **~24.7 renders/s, ~287k ciclos/render**. La nuestra: **~10.6 fps, ~670k ciclos/frame** ⇒ **2.4x más lenta**.

Desglose instrumentado con el contador de ciclos del periférico (`0xB7E928`, `out/tmp/prof116.mjs`):

| Sección | ciclos/frame | fracción |
|---|---|---|
| `clear` (1 blit, 4 planos contiguos) | 88,920 | 13 % |
| `transform` (transform + culling + visibilidad) | **176,152** | 26 % |
| `edges` (contorno `ONEDOT`+EOR) | 147,648 | 22 % |
| `fill` (area fill XOR, 1024 líneas) | **143,420** | 21 % |
| `draw` (edges+fill) | 291,068 | — |
| `update` (total) | 556,344 | 83 % |
| bucle/`render`/espera de VB | ~114k | 17 % |

Datos que acotan el problema: el `transform` CPU (176k) y el `fill` (143k) **ya superan cada uno** el total del original (287k); `clear`+`fill` juntos (232k) son ≈ todo un frame suyo. Es decir, si el original hace el mismo `clear`+`fill` (mismo `bltsize`: anchura 16 words, altura 0 = 1024 líneas), su `transform`+`edges` tendrían que caber en ~55k, frente a nuestros 324k. Optimizaciones ya aplicadas en esta pasada: `clear` de **1 blit** (antes 4) y eliminación del array `touch[2048]` muerto (ambos ~ -45k). Pendiente: investigar por qué `transform` (C++ vs asm del original) y `edges` cuestan tanto, y confirmar el modelo de coste del Blitter del emulador (ver informe para IA en `docs/debugging/`).



