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
- **Proyección**: port `1:1` de `TransformVertices` (`>>4`, `normfx`, `div16`),
  centrado en `WIDTH/2`, `HEIGHT/2`.
- **Relleno**: las **aristas visibles** se dibujan con **`MinimalBackend::blitter_line_eor`**
  (`ONEDOT`+EOR, replicadas en cada plano con el bit del color de arista; comunes fijados
  `1×/frame` con `blitter_lines_eor_begin` y Bresenham `1×/arista` con
  `blitter_line_eor_prepare/draw`, como el `DrawObject` original) y después **un único
  `MinimalBackend::blitter_area_fill`** (`FILL_XOR` + `BLITREVERSE`, altura 0) rellena el
  interior. `BLTDPTR` se deja en la **base del bitmap** (no en la dirección calculada de la
  línea): en modo línea el primer píxel va por el canal D, y mantenerlo en la base conserva la
  **paridad par/impar del contorno en los vértices**, de modo que el area fill `XOR` no filtra
  la raya horizontal por vértice. Ruta alternativa por cara (`-DFLATSHADE_FAITHFUL=0`):
  `blitter_fill_polygon` (máscara + cookie-cut).
- **Rendimiento**: el original activa `DMAF_BLITHOG`; se replica con
  `MinimalBackend::set_blitter_priority(true)` (BLTPRI, 0x0400). Con pipeline+BLITHOG el
  `update` queda bajo 284k (2 vblanks) y el frame emulado en ~20.7 fps. El `fill` coincide
  con el del original (131.8k vs 131k según su profiler), así que la brecha restante es el
  codegen del `transform` (1.8x) y de los `edges` (1.9x), no el emulador.

## Ruta ASM

`support/flatshade_asm.s` porta a asm m68k (gas, registros fijos) las rutinas calientes
del original, siguiendo el patrón de `fire_loop.s` (demo 080): `fs_update_face_visibility`,
`fs_update_edge_visibility_convex`, `fs_transform_vertices` y `fs_draw_edges`. El flag
`K_FLATSHADE_ASM` (**default 0**, versión C++ canónica) elige entre la ruta C++ y la asm.

**La ruta asm aún no es válida.** `fs_update_face_visibility`, `fs_update_edge_visibility_convex`
y `fs_transform_vertices` son correctas (con el `draw_edges` C++ encima el balón sale bien),
pero **`fs_draw_edges` dibuja el contorno con un desfase de ~1-2 px** respecto a la ruta C++.
Como el **area fill es XOR** (conmuta el relleno en cada píxel del contorno y lo propaga por
paridad de scanline), ese desfase rompe la paridad y el relleno se desmadra en bandas y
triángulos. `verify-116` da PASS (cobertura y nº de tonos no detectan el desfase) — **el gate
válido aquí es visual**: secuencia + Ollama preguntando por anomalías, o comparar el wireframe
con `-DFLATSHADE_SKIP_FILL=1`. Medido con el wireframe: ~2900 px de contorno en ambas rutas
pero solo ~96 en común. Mientras no se iguale píxel a píxel, `K_FLATSHADE_ASM=1` es solo para
depurar la ruta asm (no produce la imagen esperada).

Dos trampas ya conocidas al depurar la ruta asm: `.Lwait_blit` debe **preservar `d0`**
(`fs_draw_edges` lo usa como BLTCON0 y lo escribe justo tras el wait; si lo pisa se programa
DMACONR como con0 y ningún blit de línea pinta, balón ausente sin crashear), y el bucle
reutiliza `d5` como `x1` (no sirve de contador; `fs_draw_edges` cuenta con slots de pila).
Detalles en la bitácora `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`.

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
bash ./tools/build/build-demo.sh demos/amiga/116_flatshade_convex --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/116_flatshade_convex
node tools/analyze/verify-116-flatshade.mjs
```

Fidelidad (vs captura del original por `.adf`): fondo `#001122`, **IoU de máscara 93–97 %**
en frames alineados por fase, **0.00 % de huecos internos** (el original también 0.00 %),
cobertura ~38.5 % vs 38.9 %.

## Reutilización

`eng::object3d` (`object3d.hpp`, HOST-014) + `math2d`/`math3d` (HOST-010/011) +
`MinimalBackend::blitter_line_eor` / `blitter_area_fill`. El arreglo de **`blit_fill_region`**
a descendente (port de `BlitterFillArea`) beneficia también a las rutas Blitter del 078.
Modelo y paleta en `src/data/` (copiados del original).
