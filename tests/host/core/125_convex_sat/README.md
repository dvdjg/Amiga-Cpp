# HOST-125: SAT 2D para polígonos convexos

Test host de las adiciones a `engine/include/eng/core/util/collision.hpp`:
`convex_overlap(a, b)` (teorema de los ejes separadores) y `point_in_convex(p, poly)`.
Sin `float`, sin división y con productos de 16×16 (`muls.w`); los ejes van sin
normalizar (solo importa el signo).

## Qué comprueba

1. Cuadrados alineados: solape, separados y **tocar por el borde = separado**.
2. **Rombo** (ejes no alineados) contra cuadrado: valida los ejes de arista, no solo X/Y.
3. Independencia del **sentido de giro** (CCW/CW) y triángulo contra cuadrado.
4. `point_in_convex`: dentro, fuera, en borde y en vértice.
5. Polígono degenerado (< 3 vértices) → `false`.

## Salida de referencia

```
ConvexSAT:
OK: SAT 2D (cuadrados, rombo, sentido de giro, bordes y punto)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/125_convex_sat
```
