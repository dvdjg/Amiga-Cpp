# 116 — flatshade-convex (importe fiel de `demoscene-repo-orig`)

Objeto **convexo** (`pilka`, malla `obj2c`) girando con **sombreado plano**, importado
con el método de `docs/demos/effects/DEMOSCENE_EFFECT_REPLICATION_POLICY.md`. Es el
importe del efecto original `demoscene-repo-orig/effects/flatshade-convex/flatshade-convex.c`;
ver el plan `docs/demos/effects/FLATSHADE_CONVEX_PORT_PLAN.md`.

## Algoritmo (fiel a `lib3d`, paso a paso)

```text
  frame
    ├─ update_object_transformation   (Rx·Ry·Rz·S·T + invertida; HOST-011)
    ├─ update_face_visibility         (dot(normal, camera-p0); luz 0..15 con InvSqrt)
    ├─ update_edge_visibility_convex  (XOR de la luz de las caras adyacentes -> aristas)
    ├─ transform_vertices             (transform + proyeccion div16 + centro)
    ├─ draw_object                    (blitter_line_eor: ONEDOT+EOR por plano segun color)
    └─ blitter_area_fill              (area fill XOR: rellena el hueco dejado por el contorno)
  commit: swap de copperlist (VBlank)
```

- **Back-face culling + luz**: `v = normal·(camera − p0)`; si `v ≥ 0` la cara es
  visible y su color es `(hi16(v) · InvSqrt[hi16(|cam−p0|²)]) >> 16` (0..15), **sin
  `sqrt` en runtime** (tabla `kInvSqrt[512]`, port de `UpdateFaceVisibility`). Caras
  `material < 0` son de doble cara.
- **Aristas convexas**: por cada cara visible se marca el vértice y se hace
  `EDGE(e)->flags ^= face->flags`. El XOR cancela las aristas compartidas por dos
  caras visibles; quedan la **silueta** y las aristas visibles, con color = luz de la
  cara (o XOR de las adyacentes).
- **Proyección**: port `1:1` de `TransformVertices` (`>>4`, `normfx`, `div16`),
  centrado en `WIDTH/2`, `HEIGHT/2`.
- **Trazado**: cada arista visible se dibuja con `MinimalBackend::blitter_line_eor`
  (Blitter line mode `ONEDOT` + minterm `EOR`, secuencia exacta de `DrawObject`) en
  cada plano cuyo bit esté en el color; después `blitter_area_fill` (area fill `XOR`,
  port de `BitmapFillFast`) rellena el contorno.

## Fidelidad (comparado con el original)

Medido sobre la captura del original (`.adf` en WinUAE) y la nuestra:

| Métrica | Original | Réplica |
|---|---|---|
| Fondo | `#001122` (índice 0) | `#001122` (índice 0) |
| Caja del balón | 494×498 | 498×478 |
| Cobertura | 38.9 % | 39.7 % |
| Luminancia media del cuerpo | 149.3 | 145.9 |
| Descripción (Ollama/qwen3-vl) | poliedro flat-shaded azul, caras sólidas, fondo negro, aristas por contraste | idem |

Misma resolución (256×256×4), misma paleta, mismo modelo y misma técnica
(aristas convexas + area fill XOR).

## Verificación

```bash
bash ./tools/build/build-demo.sh demos/amiga/116_flatshade_convex --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/116_flatshade_convex
node tools/analyze/verify-116-flatshade.mjs
```

## Reutilización

`eng::object3d` (`object3d.hpp`, HOST-014) + `math2d`/`math3d` (HOST-010/011) +
`MinimalBackend::blitter_line_eor`/`blitter_area_fill` (nuevos, port de
`DrawObject`/`BitmapFillFast`). Modelo y paleta en `src/data/` (copiados del original).
