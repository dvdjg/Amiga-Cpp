# HOST-014 — modelo de objeto 3D (`eng::object3d`, port de `lib3d`)

Test host del modelo de objeto/malla del demoscene portado 1:1
(`engine/include/eng/core/object3d.hpp`): formato empaquetado de `obj2c`, acceso por
offsets de byte y `UpdateObjectTransformation` (sobre `math2d`/`math3d`).

## Qué valida

- `new_object3d` enlaza el mesh (`objdat`/grupos) sin reservar memoria.
- Offsets de las macros: `node3d(i) = objdat + i - 2`, `point3d(i)`/`vertex3d(i)`
  apuntan a los campos del `Node3D` del vértice `i` (offsets de byte).
- `update_object_transformation`: identidad + traslación en `objectToWorld`,
  traslación inversa en `worldToObject` y cámara en espacio objeto.

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/014_object3d
```
