# HOST-013 — malla 3D (`eng::math3d::MeshView`)

Test host del modelo de malla sobre `math3d` (lib3d): transformación por lotes,
**back-face culling** y **orden de pintado** (painter's algorithm) sin asignación
dinámica (`engine/include/eng/core/mesh3d.hpp`).

## Qué valida

- `MeshView` (vértices compartidos + caras por índice) y sus tamaños.
- `mesh_transform`: aplica `Mat3x3` a todos los vértices (identidad y por lotes).
- `mesh_painter_order`: descarta caras ocultas (`face_visible`) y ordena las
  visibles de lejos a cerca por `face_z_min` (shell sort in-place).
- Robustez: cara con índice fuera de rango descartada; capacidad de salida menor
  que las caras visibles se acota sin desbordar.

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/013_math3d_mesh
```
