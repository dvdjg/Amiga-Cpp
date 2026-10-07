# HOST-129: intervalos y conjunto ordenado

Test host de `engine/include/eng/core/util/interval.hpp`: `IntervalT<T>` (rango semiabierto
`[lo, hi)`; alias histórico `Interval = IntervalT<s32>`) e `IntervalSet<N,T>` (conjunto de
intervalos **disjuntos, ordenados y fusionados**, capacidad fija, sin heap). `contains` es
búsqueda binaria. El tipo del extremo es parámetro (se prueba también `u16`).

## Qué comprueba

1. `add` fusiona intervalos que **solapan o son adyacentes** (por ambos lados).
2. `contains` respeta los **bordes semiabiertos** (`lo` dentro, `hi` fuera).
3. Un rango contenido no cambia nada; un rango vacío se ignora.
4. Capacidad (`IntervalSet<N>` lleno) y `interval_overlaps` (tocarse no solapa).
5. `IntervalSet<8, u16>`/`IntervalT<u16>`: mismos invariantes con extremos sin signo.

## Salida de referencia

```
Interval:
OK: Interval (fusion de solapes/adyacencias, contains, capacidad)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/129_interval
```
