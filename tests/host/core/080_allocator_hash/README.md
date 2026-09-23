# HOST-080 — asignadores y hashes

Respalda `engine/include/eng/core/util/allocator.hpp` y
`engine/include/eng/core/util/hash.hpp`.

## Qué cubre

- **`Allocator`** (concepto) y sus implementaciones `NullAlloc` (no asigna),
  `BumpAlloc` (bump sobre una región externa) e `InlineAlloc<N>` (buffer propio).
  Se comprueba la alineación, el no-solape, la contabilidad y el rechazo controlado
  cuando no cabe.
- **`hash.hpp`**: `hash_u8/u16/u32`, `hash_value` (enteros, enums, punteros),
  `hash_bytes`/`hash_string` y el functor `Hash<T>`. Se comprueba determinismo,
  dispersión de claves consecutivas y `Hash<StringView>`.

## Por qué así (68000)

El hash evita la multiplicación de 32×32 (`__mulsi3`): enteros de ≤16 bits usan un
único `mulu.w`; de 32 bits y punteros, rotaciones/xors/sumas; y las cadenas
`h = h·33 + byte` con `h<<5 + h`. La sonda de codegen (`hash_probe.cpp`, `-S`) no
muestra `__mulsi3`, `__udivsi3` ni instrucciones de 68020.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/080_allocator_hash
```
