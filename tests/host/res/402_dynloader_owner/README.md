# HOST-402 — DynLoader propietario (R6.6)

`eng/res/dynloader.hpp`: `DynLoader::load(h, image, MemoryManager&, policy)` **posee** la memoria
(HUNK por banco vía R6.3; `.englib` copiado a un bloque) y `unload(h, mem)` la libera.

## Qué comprueba

- **`.englib`**: carga en `MemoryManager`, `Ready`, símbolo resoluble y el **código vive en un banco
  CPU** (no en el blob del llamador); `unload` restaura los bancos.
- **HUNK**: `Ready`, símbolo del hunk, code (Any) cae en Fast; `unload` restaura Chip y Fast.
- **Sin memoria**: `load` falla → `Error` (sin dejar memoria).
