# HOST-075 — algoritmos sobre Span

Respalda `engine/include/eng/core/util/algorithm.hpp`: los recorridos y
transformaciones genéricas que no cubre `eng/core/sort.hpp`.

## Qué cubre

- **Búsqueda**: `find`, `find_if`, `contains`, `count`, `count_if`.
- **Cuantificadores**: `all_of`, `any_of`, `none_of`.
- **Recorrido**: `for_each`, `transform` (in situ).
- **Copia y relleno**: `copy`, `copy_n`, `fill_n` (como mucho el destino; devuelven
  cuántos escribieron), `equal`.
- **Reducción**: `accumulate` (suma y con operación), `min_element`, `max_element`.
- **Búsqueda binaria**: `lower_bound`, `upper_bound`, `binary_search`.
- **Permutación**: `reverse`, `rotate` (tres inversiones), `iota`.
- **Compactación**: `remove_if`, `unique` (devuelven el `Span` acotado).

## Notas

- Ninguno asigna: operan sobre memoria contigua ya existente.
- Toman `Span<T>` (no `Span<const T>`) porque `eng::Span` no convierte mutable a
  const de forma implícita; los algoritmos de lectura funcionan igual.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/075_algorithm
```
