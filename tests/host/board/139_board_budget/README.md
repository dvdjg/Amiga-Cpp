# HOST-139: presupuesto de memoria de `eng::board`

Test host de `engine/include/eng/board/core/budget.hpp`: perfiles de footprint
`P20`…`P1M`, selección del mayor que cabe en la RAM libre y reparto entre tabla de
transposición, libro, caché externa y pila de búsqueda.

## Qué comprueba

1. Footprint planificado de cada perfil (≈21 kB, 58 kB, 122 kB, 246 kB, 474 kB,
   954 kB) con el tamaño de entrada de TT fijado en 12 B.
2. Selección determinista por RAM libre: 20 kB→`P20`, 64 kB→`P64`, 128 kB→`P128`,
   256 kB→`P256`, 512 kB→`P512`, 1 MB→`P1M`; 0 bytes cae al fallback `P20`.
3. Monotonía: la TT no decrece al crecer la RAM libre; `P20` no usa TT y `null_move`
   solo aparece a partir de `P256`; `P1M` ofrece Multi-PV 4.

## Salida de referencia

```
eng::board budget:
OK: eng::board budget (perfiles, seleccion, monotonia)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/139_board_budget
```
