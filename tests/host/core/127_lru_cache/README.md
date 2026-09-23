# HOST-127: caché LRU de capacidad fija

Test host de `engine/include/eng/core/util/lru_cache.hpp`: `LruCache<K, V, N>`, caché
**LRU** sin heap con `get`/`put`/`erase` en `O(1)` (índice hash + lista doblemente
enlazada intrusiva). Generaliza el patrón de `eng::field::ChunkCache`.

## Qué comprueba

1. `put`/`get`/`size`/`full` y **desalojo de la entrada menos reciente** al llenarse.
2. `get` toca la entrada (la hace la más reciente); `peek` NO cambia la recencia.
3. Actualizar una clave existente no desaloja (`put` devuelve `false`).
4. `erase` (y reutilización de la ranura libre) y `clear`.

## Salida de referencia

```
LruCache:
OK: LruCache (eviccion LRU, get/peek, erase, clear)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/127_lru_cache
```
