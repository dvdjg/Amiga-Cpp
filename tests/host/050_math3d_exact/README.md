# HOST-050 — bit-exactitud de `math3d` (red de seguridad de F3)

Antes de migrar el engine a la librería genérica, esto fija que las versiones **nuevas**
dan **exactamente** los mismos valores que las viejas.

## Qué valida

- `load_rotate(ax, ay, az)` y `load_reverse_rotate(ax, ay, az)` nuevos (construidos con
  `dot`/`operator*`/`operator-` de `eng::math`) contra `eng::math3d` actual, para los
  **4096 ángulos** — con los tres ejes iguales (el caso de la demo) y con ejes
  distintos (pasos 3/7/11).
- Compara los 9 elementos de la matriz **valor a valor** (`==`). Cualquier diferencia es
  un fallo, no una mejora: la migración no debe cambiar ni un píxel.

Se apoya en que la normalización fusionada (`dot`) es bit-idéntica al `normfx(mul16)`
del original, y en que el exponente va en el tipo (misma representación `s16`, mismo
resultado).

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/050_math3d_exact
```
