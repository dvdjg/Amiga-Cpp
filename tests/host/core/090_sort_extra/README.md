# HOST-090 — ordenación ampliada

Respalda `engine/include/eng/core/sort.hpp` (las incorporaciones sobre `quick_sort`).

## Qué cubre

- **`stable_sort`** (con `scratch` de merge sort, y sin `scratch` por inserción): se
  comprueba que las claves iguales **conservan su orden de inserción**.
- **`nth_element`** (quickselect): el elemento `n` queda en su posición ordenada con
  los menores antes y los mayores después.
- **`partial_sort`**: los `n` primeros son los `n` menores, ya ordenados.
- **`is_sorted`**.
- **`radix_sort_u16`** (LSD, 2 pasadas de 8 bits, estable, sin heap): coincide con un
  `quick_sort` de referencia; rechaza `scratch` insuficiente.

## Coste

`stable_sort` con `scratch` es `O(n log n)` y `O(1)` de pila; `nth_element`/`partial_sort`
son `O(n)`/`O(n + k log k)` esperados; `radix_sort_u16` usa 1 KiB de pila (256
contadores `u32`) y no depende de la distribución.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/090_sort_extra
```
