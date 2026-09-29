# HOST-351 - mem_address

Test de `Address<K>` (`eng/core/types/memory_kind.hpp`).

## Que cubre

- **Banco en el tipo**: `Address<MemoryKind::Chip>` y `Address<MemoryKind::Fast>` son tipos distintos
  (`static_assert`); no se pueden mezclar, asi una API DMA (Chip) no compila con otra.
- **Aritmetica de direccion** que conserva el banco: `address + offset -> address` (mismo tipo) y
  `address - address -> offset` (`uintptr`), con `+=`/`-=` y comparacion/orden. Evita el `cast` al
  pasar offsets puros a una API DMA.
- **Frontera con puntero**: construccion explicita desde `const void*` y salida por `cptr()`/`ptr()`,
  una sola vez por cambio de representacion.

## Como se ejecuta

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/351_mem_address
```
