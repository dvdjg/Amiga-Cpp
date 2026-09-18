# HOST-135: matriz de escalares

Test host que instancia los **mismos** algoritmos genéricos con varios escalares y los
compara contra una referencia `double`, midiendo el error relativo. Sirve de referencia de
cómo se comporta cada ancho (16 bits retro vs 32 bits vs nativo) y cierra las fases **F0**
(`scalar.hpp`) y **F1** (división/raíz de `Fixed<s32,E>`) del
[roadmap del escalar genérico](../../../docs/guides/roadmap/REFACTOR_SCALAR_GENERICO.md).

## Qué comprueba

1. `lerp`/`smoothstep`/`normalize` con `double`, `float`, `MiniFloat16`, `Fixed<s16,12>`,
   `Fixed<s32,12>` y `Fixed<s32,24>`; el error máximo por escalar debe quedar bajo su
   tolerancia documentada.
2. `div_norm` y `scalar_sqrt` de `Fixed<s32,12>` (F1) contra valores exactos y contra
   `1/3`/`sqrt(3)`.
3. El escalar entero general `eng::intw` (`scalar.hpp`) con `min`/`max`/`abs`/`clamp`, y el
   modo seleccionado (`eng::scalar_mode`).
4. `smootherstep` con `Fixed<s32,12>`: el coeficiente 15 no cabe en 4.12 (`s16`, ±8) pero sí
   con 32 bits; es la fila `smootherstep` de la columna `Fixed<s32>` de la tabla de
   `SCALAR_LIBRARY.md` §7.

## Salida de referencia (host, modo nativo)

```
  double         lerp=0.00e+00 smooth=0.00e+00 norm=0.00e+00 (tol 1e-12)
  float          lerp=3.97e-08 smooth=9.49e-08 norm=1.71e-08 (tol 1e-05)
  MiniFloat16    lerp=2.79e-04 smooth=1.18e-03 norm=1.07e-04 (tol 5e-03)
  Fixed<s16,12>  lerp=9.77e-04 smooth=7.00e-03 norm=1.07e-04 (tol 1e-02)
  Fixed<s32,12>  lerp=9.77e-04 smooth=7.00e-03 norm=1.07e-04 (tol 1e-02)
  Fixed<s32,24>  lerp=2.38e-07 smooth=2.23e-06 norm=1.71e-08 (tol 1e-05)
```

Con `-DENG_SCALAR_RETRO16` se fuerza en host la selección de 16 bits (simulación).

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/135_scalar_matrix
```
