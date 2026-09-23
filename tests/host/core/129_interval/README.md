# HOST-129: intervalos y conjunto ordenado

Test host de `engine/include/eng/core/util/interval.hpp`: `Interval` (rango semiabierto
`[lo, hi)`) e `IntervalSet<N>` (conjunto de intervalos **disjuntos, ordenados y
fusionados**, capacidad fija, sin heap). `contains` es búsqueda binaria.

## Qué comprueba

1. `add` fusiona intervalos que **solapan o son adyacentes** (por ambos lados).
2. `contains` respeta los **bordes semiabiertos** (`lo` dentro, `hi` fuera).
3. Un rango contenido no cambia nada; un rango vacío se ignora.
4. Capacidad (`IntervalSet<N>` lleno) y `interval_overlaps` (tocarse no solapa).

## Salida de referencia

```
Interval:
OK: Interval (fusion de solapes/adyacencias, contains, capacidad)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/129_interval
```
