# 116 — flatshade-convex (importe de `demoscene-repo-orig`)

Objeto **convexo** (`pilka`, malla `obj2c`) girando con **sombreado plano por cara**:
cada cara visible se rellena con su color de luz (0..15) y el relleno se materializa
con el Blitter. Es el importe optimizado en C++ del efecto original
`demoscene-repo-orig/effects/flatshade-convex/flatshade-convex.c`.

## Algoritmo (fiel a `lib3d`)

```text
  frame
    ├─ update_object_transformation   (Rx·Ry·Rz·S·T + invertida; HOST-011)
    ├─ update_face_visibility         (dot(normal, camera-p0), luz con InvSqrt[512])
    ├─ mark_visible_vertices          (flags de los vertices de caras visibles)
    ├─ transform_vertices             (transform + proyeccion div16 + centro)
    └─ build_flat_triangles           (fan-triangula caras visibles, color = luz)
         └─ MinimalBackend::fill_triangles_blitter  (mascara 1 bit + area fill + cookie-cut)
```

- **Back-face culling + luz**: `v = normal·(camera - p0)`; si `v >= 0` la cara es
  visible y su color es `(hi16(v) * InvSqrt[hi16(|cam-p0|²)]) >> 16` (0..15), **sin
  `sqrt` en runtime** (tabla `InvSqrt[512]`, port de `UpdateFaceVisibility`). Caras
  `material < 0` son de doble cara.
- **Proyección**: el port `1:1` de `TransformVertices` (`>>4`, `normfx`, `div16`),
  centrado en `WIDTH/2`, `HEIGHT/2`.

## Diferencias con el original (intencionadas)

- El original dibujaba las **aristas visibles** (visibilidad convexa por XOR) y
  rellenaba con un **area-fill XOR**; aquí se rellena **por cara** (fan-triangulada)
  con su color de luz, que es el mismo modelo de sombreado plano y reutiliza el
  Blitter del engine (`fill_triangles_blitter`, el mismo camino que la demo 078).
- Display y doble buffer por swap de copperlist (el original usa 2 buffers + parcheo
  de `BPLxPT`).

## Verificación

```bash
bash ./tools/build/build-demo.sh demos/amiga/116_flatshade_convex --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/116_flatshade_convex
node tools/analyze/verify-116-flatshade.mjs
```

El verificador comprueba que hay un balón relleno (blob no-fondo), aproximadamente
redondo y centrado, con varios tonos de luz.

## Reutilización

`eng::object3d` (`object3d.hpp`, HOST-014) + `math2d`/`math3d` (HOST-010/011) +
`MinimalBackend::fill_triangles_blitter`. Modelo y paleta en `src/data/` (copiados
del original; la paleta es la misma que la demo 079).
