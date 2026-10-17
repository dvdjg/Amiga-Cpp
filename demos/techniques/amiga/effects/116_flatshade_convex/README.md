# 116 — flatshade-convex (importe fiel de `demoscene-repo-orig`)

Objeto **convexo** (`pilka`, malla `obj2c`) girando con **sombreado plano**, importado
con el método de `docs/demos/effects/DEMOSCENE_EFFECT_REPLICATION_POLICY.md`. Es el
importe del efecto original `demoscene-repo-orig/effects/flatshade-convex/flatshade-convex.c`;
ver el plan `docs/demos/effects/FLATSHADE_CONVEX_PORT_PLAN.md`.

## Algoritmo

```text
  frame (pipeline de 3 buffers, lookahead 1 frame)
    ├─ update(N)
    │    ├─ esperar el fill del buffer mostrado (lanzado en update(N-1))
    │    ├─ swap: mostrar el buffer dibujado (y rellenado) el frame pasado
    │    ├─ draw_edges            (aristas ONEDOT+EOR por plano según el color)
    │    ├─ lanzar fill (sin esperar; lo absorbe el swap del update siguiente)
    │    ├─ precalcular estado(N+1)   (transform + luz + visibilidad) DURANTE el fill
    │    └─ lanzar clear(N+1)         (el buffer del próximo update) DURANTE el fill
    └─ commit: swap de copperlist (VBlank)
```

Con **3 buffers** el buffer a pre-limpiar ni se muestra ni se dibuja, así que su clear y el
transform del frame siguiente quedan escondidos bajo el fill del frame actual; con 2 buffers el
clear del buffer trasero es inseparable del camino crítico. El fill se lanza sin esperarlo
(`wait=false`): el buffer nunca se ve a medias porque se muestra en el swap del update siguiente,
cuyo primer `wait_blitter` ya ha absorbido el fill. La secuencia de rotación mostrada es la del
original (latencia de 1 frame, mismo orden de ángulos).

- **Back-face culling + luz**: `v = normal·(camera − p0)`; si `v ≥ 0` la cara es
  visible y su color es `(hi16(v) · kInvSqrt[hi16(|cam−p0|²)]) >> 16` (0..15), **sin
  `sqrt` en runtime** (tabla `kInvSqrt[512]`, port de `UpdateFaceVisibility`). Caras
  `material < 0` son de doble cara.
- **Proyección**: port `1:1` de `TransformVertices` (`>>4`, `normfx`, `div_wide`),
  centrado en `WIDTH/2`, `HEIGHT/2`.
- **Relleno**: las **aristas visibles** se dibujan con **`AmigaBackend::blitter_line_eor`**
  (`ONEDOT`+EOR, replicadas en cada plano con el bit del color de arista; comunes fijados
  `1×/frame` con `blitter_lines_eor_begin` y Bresenham `1×/arista` con
  `blitter_line_eor_prepare/draw`, como el `DrawObject` original) y después **un único
  `AmigaBackend::blitter_area_fill`** (`FILL_XOR` + `BLITREVERSE`, altura 0) rellena el
  interior. `BLTDPTR` se deja en la **base del bitmap** (no en la dirección calculada de la
  línea): en modo línea el primer píxel va por el canal D, y mantenerlo en la base conserva la
  **paridad par/impar del contorno en los vértices**, de modo que el area fill `XOR` no filtra
  la raya horizontal por vértice. Ruta alternativa por cara (`-DFLATSHADE_FAITHFUL=0`):
  `blitter_fill_polygon` (máscara + cookie-cut).
- **Rendimiento**: el original activa `DMAF_BLITHOG`; se replica con
  `AmigaBackend::set_blitter_priority(true)` (BLTPRI, 0x0400). Con pipeline+BLITHOG el
  `update` queda bajo 284k (2 vblanks) y el frame emulado en ~20.7 fps. El `fill` coincide
  con el del original (131.8k vs 131k según su profiler), así que la brecha restante es el
  codegen del `transform` (1.8x) y de los `edges` (1.9x), no el emulador.

## Ruta ASM

`support/flatshade_asm.s` porta a asm m68k (gas, registros fijos) las rutinas calientes
del original, siguiendo el patrón de `fire_loop.s` (demo 080): `fs_update_face_visibility`,
`fs_update_edge_visibility_convex`, `fs_transform_vertices` y `fs_draw_edges`. El flag
`K_FLATSHADE_ASM` (**default 0**, versión C++ canónica) elige entre la ruta C++ y la asm.

**La ruta asm aún no es válida.** `fs_update_face_visibility`, `fs_update_edge_visibility_convex` y `fs_transform_vertices` son correctas (con el `draw_edges` C++ encima el balón sale bien), pero **`fs_draw_edges` no reproduce el contorno**. Historial: en septiembre salía con un desfase de ~1-2 px (wireframe: ~2900 px de contorno en ambas rutas, solo ~96 en común), y en la pasada de 2026-10 el build `K_FLATSHADE_ASM=1` **dibuja rayas** y no avanza frames — se corrigió entremedias una causa concreta (la elección de octante usaba `x1 < x0` en lugar de `x0 >= x1` para AUL/SUL; contraste con `line_prepare_impl`), pero hay más deuda. Como el **area fill es XOR** (conmuta el relleno en cada píxel del contorno y lo propaga por paridad de scanline), cualquier desfase rompe la paridad y el relleno se desmadra. **El gate válido es visual**: secuencia + Ollama preguntando por anomalías, o comparar el wireframe con `-DFLATSHADE_SKIP_FILL=1` (`verify-116` da PASS con la imagen rota: no detecta desfases de contorno). Diagnóstico siguiente en DT-026: `freeze-diff` de `fs_transform_vertices` vs `lib3d` sobre el wireframe.

Dos trampas ya conocidas al depurar la ruta asm: `.Lwait_blit` debe **preservar `d0`** (`fs_draw_edges` lo usa como BLTCON0 y lo escribe justo tras el wait; si lo pisa se programa DMACONR como con0 y ningún blit de línea pinta, balón ausente sin crashear), y el bucle reutiliza `d5` como `x1` (no sirve de contador; `fs_draw_edges` cuenta con slots de pila). Detalles en la bitácora `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`.

## Variantes de ruta (`FLATSHADE_ASYNC` y ablaciones)

- `-DFLATSHADE_ASYNC=1`: ejecuta contorno + fill acotado + clear por plano como **cadenas del referee async** (`FramePlan` encadenado por la IRQ de fin de blit; fix de engine `g_blit_service_referee_only`). Funciona y no cuelga, pero **es más lenta en esta demo** (8,9 fps / 5,6 campos vs 18,05 / 2,77): ~51 jobs cortos de línea pagando arranque de Blitter + IRQ + re-programación por job. Se conserva como vía documentada (útil para cadenas cortas de blits largos); el default es la síncrona. Diagnóstico completo en DT-026.
- `-DFLATSHADE_CLEAR_BBOX=1` (release): clear acotado a la union de bboxes — **19,24 fps vs 19,26** del barrido completo: el clear ya está oculto en la pipeline de 3 buffers, no hay nada que cobrar.
- `-DFLATSHADE_FILL_BBOX=1` (release): fill acotado a bbox — **16,81 fps vs 19,26**: la serialización de sus 4 jobs dentro del update se come el recorte (confirma la lección de septiembre, ahora medida en aislado).

## Paridad del contorno (clave del relleno)

El area fill `XOR` del Blitter conmuta el relleno en cada píxel del contorno de la fila y
lo propaga verticalmente. Para que rellene solo el interior, cada scanline debe cruzar el
contorno un número impar de veces. Cada arista se dibuja con `ONEDOT` (un píxel por fila)
y minterm **EOR**; en un vértice dos aristas pueden escribir el mismo píxel y cancelarse
(`1 XOR 1 = 0`), perdiendo un cruce y filtrando una raya. El truco de `BLTDPTR` = base del
bitmap (el primer píxel de la línea va por D a un punto inofensivo) y el descarte de
**aristas horizontales** (`y0 == y1`) mantienen la paridad correcta; el resultado tiene
**0.00 % de huecos internos**, igual que el original (ver `FLATSHADE_CONVEX_PORT_PLAN.md`).

## Verificación

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/116_flatshade_convex --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/116_flatshade_convex
node tools/analyze/verify-116-flatshade.mjs
```

Fidelidad (vs captura del original por `.adf`): fondo `#001122`, **IoU de máscara 93–97 %**
en frames alineados por fase, **0.00 % de huecos internos** (el original también 0.00 %),
cobertura ~38.5 % vs 38.9 %.

### Gate de fase congelada (bit-exactitud de render)

`verify-116` mira cobertura/tonos/forma, y sus números **varían con la fase** de giro del
balón: no sirven para decidir si un cambio altera la imagen. Para eso está el ángulo
fijo, que hace la captura **determinista**:

```bash
# mismo ángulo en las dos builds que se comparan (p. ej. antes/después de un cambio)
sed -i 's/#define FLATSHADE_FREEZE_ANGLE 0/#define FLATSHADE_FREEZE_ANGLE 1000/' src/main.cpp
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/116_flatshade_convex --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/116_flatshade_convex
cp out/run/116_flatshade_convex/A500_debug/screenshot.png /tmp/a.png
# ... cambiar a la otra build ...
node tools/analyze/freeze-diff.mjs /tmp/a.png out/run/116_flatshade_convex/A500_debug/screenshot.png
```

Con el ángulo congelado dos ejecuciones dan el **mismo PNG byte a byte**
(`freeze-diff` → `MAD/px=0`), así que `MAD != 0` significa que el cambio toca la imagen.
Es el gate que faltaba para validar optimizaciones de render sin caer en comparar fases
distintas.

## Reutilización

Las rutinas de efectos (visibilidad de caras con luz, visibilidad de aristas de un
sólido convexo y transform + proyección de vértices) **son API del engine**:
`eng::lib3d` en `engine/include/eng/platform/amiga/lib3d.hpp`, con test host **HOST-047** (y no
viven en la demo). Se apoyan en `eng::object3d` (`object3d.hpp`, HOST-014) y en
`lib2d`/`math3d` (HOST-010/011); el dibujo usa
`AmigaBackend::blitter_line_eor` / `blitter_area_fill`.

En la demo queda **solo lo específico del efecto**: la orquestación (pipeline de 3
buffers, cobre, paleta) y el dibujo Amiga (líneas EOR + area fill XOR), que se
pueden reutilizar tal cual en otros efectos de contorno+relleno. El arreglo de
**`blit_fill_region`** a descendente (port de `BlitterFillArea`) beneficia también a
las rutas Blitter del 078. Modelo y paleta en `src/data/` (copiados del original).

## Modernización (2026-10)

- **Bucle por IRQ** (`Engine::run_frames` + `Wait()`) en lugar de sondeo de VPOSR, y `set_blit_service_enabled(false)` (la demo no usa la cola de fondo): las esperas del Blitter son sondeo apretado sin llamada de servicio por vuelta.
- **Franja de depuración de coste cero** (norma del repo): nombre + versión + contador de frames + **fps del último segundo** en un **5º plano de 1 bpp activo todo el frame** (etapa de `scene::compose`; sin zonas de Copper: una zona `>255` no se puede cerrar con el comparador `>=` de WinUAE (`custom.cpp:4416` `coppercomp`) y dejaba el plano encendido — franjas de basura); la parte estática se pinta una vez y los campos dinámicos ~1 vez/segundo (~56 escrituras de palabra; `eng::debug::HudFpsWindow`). El render por frame no cambia; el plano del HUD es de tamaño completo (8 KB) y la paleta 16..31 es un **espejo** de la del sombreado (16 = tinta) para que el +16 del plano sea transparente en la escena. La franja queda en la banda superior (filas de ventana ≈16..23).
- **Soporte `bbox`** en `eng::retro::flat_shade_xor` (fill XOR acotado a la caja del objeto): en esta pipeline síncrona **empeora** el frame (4 jobs por plano que esperan entre sí dentro del update; medido 19,2 → 17,3 fps). Queda como primitivo para el camino async y como lección registrada en DT-026.
- Medida: **18,05 fps / 2,77 campos/frame** (`A500_debug`) y **19,22 fps / 2,60 campos** (`A500_release`; HEAD: 19,20 fps; el fetch del 5º plano del HUD cuesta ~0,23 campos); el fps que muestra el propio HUD coincide con la medida. Sigue por debajo del suelo de 25 fps del efecto de relleno de polígonos → demo **ABIERTA** por presupuesto (DT-026); el camino es el **referee async** del `FramePlan` (encadenado por IRQ; la base `BlitJobKind::AreaFill` ya existe en el engine; el prototipo quedó colgado y se retiró). Detalle de medidas y validación: `VALIDATION.md`.

## Contra la referencia original (¿por qué no la igualamos aún?)

La pregunta «la original va como el doble de rápida, ¿por qué no la igualamos?» tiene una medida detrás (2026-10-17): la original (`flatshade-convex.adf`, emulador ciclo-exacto, perfil WinUAE con capturas por campo) **no mete el frame en un campo** — sus frames salen en parejas de campos idénticos, es decir **2,0 campos/frame (25 fps)**, coherente con los números de su propio profiler (transform 156 + draw 130 + fill 289 = 575 líneas ≈ 1,84 campos). El listón realista es 25 fps, no 50.

Brecha medida (por frame completado, perfiles con los mismos contadores DMA del recorder de WinUAE): original 2,0 campos × ~62k accesos/campo ≈ 124k; este port 2,6 × ~63k ≈ 163k. El exceso es sobre todo **CPU (+45 % de accesos: 51,5k vs 35,5k por frame** — codegen `-O2` de g++ frente al gcc de Bebbo con pistas `register asm` del original, más plan/servicio por job) y **fetch de bitplano** (+~0,2 campos/frame: 5º plano del HUD y ventana compartida); el trabajo de Blitter por frame es similar. La vía de cierre es la del plan de DT-026: **async** (solapar CPU y Blitter como el original, cuyo clear se absorbe bajo el transform) + la **ruta asm** del transform (1,8× medido, con el fix de contorno pendiente). Método de medida y tabla de canales: `docs/tools/PROFILING_FROM_AGENT.md` §«Cadencia de un binario de referencia»; herramientas: `tools/debug/profile-cadence.mjs` y `tools/debug/hud-zoom.mjs`.

