# HOST-055 — inversa de una transformación rígida (`math3d::inverse_rigid`)

Fija la inversa de una transformación **rígida** (rotación + traslación, sin escala ni
cizalla) de `eng/platform/amiga/gfx3d.hpp`: la parte lineal es ortonormal → `m⁻¹ = mᵀ`, y
la traslación `t⁻¹ = −mᵀ·t`. Sin divisiones.

## Qué cubre

- **`compose(a, inverse_rigid(a)) ~ identidad`** (matriz y traslación) sobre varias
  rotaciones (incluidas 90°/180°/4095) y dos traslaciones.
- **Doble inversa**: `inverse_rigid(inverse_rigid(a)) ~ a`.
- La tolerancia acota el redondeo de 4.12 (±8 en la matriz, ±4 en la traslación).

## Por qué existe (y en qué se diferencia de `object3d`)

`object3d::update_object_transformation` construye su `worldToObject` analíticamente, pero
**no es una inversa completa**: invierte la parte lineal (`S⁻¹Rᵀ`) y deja la traslación en
`−T` en vez de `−S⁻¹Rᵀ·T`. Medido con `compose(objectToWorld, worldToObject)` en un caso
rotation+scale+translate:

```
.m ≈ I            (4094 -2 0 | 1 4093 -1 | 0 0 4090)
.t = (-9965, -800, -1924)      <- debería ser (0, 0, 0)
```

Ese `−T` es el comportamiento del port 1:1 (lo fija HOST-014), así que **no se toca**: la
cámara en espacio objeto de las demos 079/116 depende de él. `inverse_rigid` es la inversa
*correcta* para el caso sin escala, y es la que se debe usar al componer.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/055_inverse_rigid
```
