# HOST-011 — math3d (lib3d, fixed-point 4.12)

Test host de `eng::core::math3d` (`engine/include/eng/core/math3d.hpp`), port de la
parte pura de **`lib3d`** de `demoscene-repo-orig`.

## Qué valida

- Matriz de identidad y `transform` con identidad.
- `translate` y `scale` (0.5, con truncado del `normfx`).
- `load_rotate` sobre Z 90° (`(x,y,z) -> (y,-x,z)`).
- `compose(I, R) == R` y `R · R⁻¹ ≈ I` (con `load_reverse_rotate`).

## Formato

Igual que `math2d`: `s16` en **4.12** (`1.0 == 4096`), `normfx(a) = a >> 12`
(shift aritmético). Reutiliza la tabla de seno de `math2d`. La composición suma la
traslación (como el `Compose3D` original).

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/011_math3d
```

Origen: `demoscene-repo-orig/lib/lib3d` (`LoadIdentity3D`, `Translate3D`,
`Scale3D`, `LoadRotate3D`, `LoadReverseRotate3D`, `Compose3D`, `Transform3D`).
