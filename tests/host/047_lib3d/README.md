# HOST-047 — lib3d (`eng::lib3d`): visibilidad de caras/aristas + transform

Test host de las rutinas puras de `lib3d` promovidas al engine
(`engine/include/eng/platform/amiga/lib3d.hpp`): visibilidad de caras con luz, visibilidad de
aristas de un sólido convexo y transform + proyección de vértices, sobre el modelo
empaquetado de `object3d`.

## Qué valida

- `hi16`: parte alta de 32 bits como `s16`.
- `kInvSqrt`: los primeros valores de la tabla `65535 / sqrt(x)`.
- `update_face_visibility`: cara que mira a la cámara (visible, luz 16) y cara de
  espaldas con `material >= 0` (oculta, `-1`).
- `update_edge_visibility_convex`: marca el nodo de la cara visible y **XOR-ea** su
  luz en la arista.
- `transform_vertices`: proyecta el vértice al centro de pantalla `(128, 128)`,
  deja `zp != 0` y actualiza la bounding-box.

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/047_lib3d
```
