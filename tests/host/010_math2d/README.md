# HOST-010 — math2d (lib2d, fixed-point 4.12)

Test host de `eng::core::math2d` (`engine/include/eng/core/math2d.hpp`), port de
la parte pura de **`lib2d`** de `demoscene-repo-orig`.

## Qué valida

- Matriz de identidad y `transform` con identidad (no altera puntos).
- `translate` (suma a la traslación) y `scale` (0.5).
- `rotate` 90° y 180° en coordenadas de pantalla (`(x,y) -> (y,-x)` y negación).
- Tabla de seno/coseno 4.12 (`sin(π/2)=1.0`, `sin(π)=0`, `cos(0)=1.0`).
- `point_flags` (lados de la ventana de recorte).

## Formato

Los valores son `s16` en **4.12** (`1.0 == 4096`); `normfx(a) = a >> 12` normaliza
los productos (como el `normfx` del 68000). La tabla de seno tiene 4096 pasos por
vuelta (8 KB en `s16`), generada en compile-time reutilizando el
`eng::SineTable` del engine (sin libm en runtime).

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/010_math2d
```

Origen: `demoscene-repo-orig/lib/lib2d` (`LoadIdentity2D`, `Translate2D`,
`Scale2D`, `Rotate2D`, `Transform2D`, `PointsInsideBox`) y `include/fx.h`
(`fx12f`, `normfx`, `SIN/COS`).
