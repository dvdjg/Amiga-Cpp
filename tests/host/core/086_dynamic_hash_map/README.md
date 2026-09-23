# HOST-086 — DynamicHashMap

Respalda `engine/include/eng/core/util/dynamic_hash_map.hpp`.

## Qué cubre

- **Crecimiento por rehash**: la tabla reserva sus tablas (claves/valores/ocupación) en
  un `Allocator` y las rehace al cruzar el 3/4 de carga; el test fuerza varios rehashes
  hasta 512 ranuras.
- **Estrés determinista** (xorshift) contra un modelo de referencia en arrays: 20.000
  operaciones de insertar/borrar/buscar con crecimiento, comparando resultado, valor y
  `size` en cada paso (cubre rehash + back-shift).
- **Internado de cadenas** con clave `StringView` (deduplica por contenido).
- **Sin allocator** (`NullAlloc`): `insert` devuelve `nullptr` y no reserva.

## Restricciones

`K` y `V` deben ser copiables trivialmente (el almacenamiento es crudo, no construye
objetos). El crecimiento desperdicia la tabla vieja (los allocators del engine son
bump): úsalo en fase `init`/carga, no en `frame`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/086_dynamic_hash_map
```
