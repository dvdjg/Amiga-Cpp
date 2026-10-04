# HOST-414 — ciclo de vida y lista por índices

Respalda `engine/include/eng/core/util/noncopyable.hpp` y
`engine/include/eng/core/util/index_list.hpp`.

## Qué cubre

- **`Noncopyable` / `NonMovable`**: bases que marcan el ciclo de vida de un tipo sin
  repetir el par de `= delete`. Se comprueba con `__is_constructible` que no se copian
  (y `NonMovable` tampoco se mueve).
- **Adopción en los contenedores**: `Vector`, `SmallVector`, `ChunkedVector` y
  `DynamicHashMap` no se copian pero sí se mueven (declaran su movimiento); `LruCache`
  no se copia ni se mueve (sus listas referencian sus arrays internos).
- **`IndexList`**: lista doble enlazada por índices con `push_front`/`push_back`/
  `erase`/`pop`/`touch_front` en `O(1)` e iteración en orden; y dos listas que
  **comparten** el par de arrays `prev`/`next` con ranuras disjuntas.

## Por qué así (68000)

Enlazar por índices de 16 bits cuesta 4 bytes por nodo (dos índices) frente a 8 de dos
punteros de 32. `LruCache` reutiliza el mismo par de arrays para la lista de recencia y
la de ranuras libres (una ranura está en una o en la otra, nunca en ambas), de modo que
sustituir su lista a mano por `IndexList` no añade memoria. Al referenciar arrays
internos, `LruCache` es `NonMovable`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/414_lifecycle_index_list
```
