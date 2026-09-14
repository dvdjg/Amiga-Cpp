# 116 — flatshade-convex (importe fiel de `demoscene-repo-orig`)

Objeto **convexo** (`pilka`, malla `obj2c`) girando con **sombreado plano**, importado
con el método de `docs/demos/effects/DEMOSCENE_EFFECT_REPLICATION_POLICY.md`. Es el
importe del efecto original `demoscene-repo-orig/effects/flatshade-convex/flatshade-convex.c`;
ver el plan `docs/demos/effects/FLATSHADE_CONVEX_PORT_PLAN.md`.

## Algoritmo

```text
  frame
    ├─ update_object_transformation   (Rx·Ry·Rz·S·T + invertida; HOST-011)
    ├─ update_face_visibility         (dot(normal, camera-p0); luz 0..15 con kInvSqrt)
    ├─ update_edge_visibility_convex  (XOR de la luz de caras vecinas; marca vertices)
    ├─ transform_vertices             (transform + proyeccion div16 + centro)
    └─ draw_edges_area_fill           (aristas ONEDOT+EOR por plano segun el color +
                                        UN blitter_area_fill XOR; BLTDPTR = base)
  commit: swap de copperlist (VBlank)
```

- **Back-face culling + luz**: `v = normal·(camera − p0)`; si `v ≥ 0` la cara es
  visible y su color es `(hi16(v) · kInvSqrt[hi16(|cam−p0|²)]) >> 16` (0..15), **sin
  `sqrt` en runtime** (tabla `kInvSqrt[512]`, port de `UpdateFaceVisibility`). Caras
  `material < 0` son de doble cara.
- **Proyección**: port `1:1` de `TransformVertices` (`>>4`, `normfx`, `div16`),
  centrado en `WIDTH/2`, `HEIGHT/2`.
- **Relleno**: las **aristas visibles** se dibujan con **`MinimalBackend::blitter_line_eor`**
  (`ONEDOT`+EOR, replicadas en cada plano con el bit del color de arista) y después
  **un único `MinimalBackend::blitter_area_fill`** (`FILL_XOR` + `BLITREVERSE`, altura 0)
  rellena el interior. `BLTDPTR` se deja en la **base del bitmap** (no en la dirección
  calculada de la línea): en modo línea el primer píxel va por el canal D, y mantenerlo
  en la base conserva la **paridad par/impar del contorno en los vértices**, de modo que
  el area fill `XOR` no filtra la raya horizontal por vértice. Ruta alternativa por cara
  (`-DFLATSHADE_FAITHFUL=0`): `blitter_fill_polygon` (máscara + cookie-cut).

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
