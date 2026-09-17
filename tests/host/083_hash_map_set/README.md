# HOST-083 — HashMap y HashSet

Respalda `engine/include/eng/core/util/hash_map.hpp` y
`engine/include/eng/core/util/hash_set.hpp`.

## Qué cubre

- **`HashMap<K, V, N>`** y **`HashSet<T, N>`**: direccionamiento abierto con sondeo
  lineal, capacidad interna potencia de dos (índice por máscara), `find`/`contains`/
  `insert`/`insert_or_assign`/`erase`/`for_each` y rechazo controlado al llenarse.
- **Estrés determinista** (xorshift) contra un modelo de referencia en arrays: miles de
  operaciones de insertar/borrar/buscar comparando resultado, valor y `size` en cada
  paso. Cubre el borrado por **desplazamiento hacia atrás** (back-shift) en todas sus
  transiciones.

## Por qué así (68000)

Potencia de dos evita `__udivsi3` en el índice; el factor de carga ≤ 3/4 deja siempre
una ranura libre (sondeo acotado); el hash (`hash.hpp`) no usa multiplicación de 32×32,
así que no hay `__mulsi3`. La sonda de codegen no muestra libcalls.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/083_hash_map_set
```
