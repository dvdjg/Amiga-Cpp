# HOST-146: fuente de bloques y caché LRU

Test host de `eng/board/storage/block_source.hpp` y `eng/board/knowledge/cache.hpp`.

## Qué enseña / comprueba

- **`BlockSource`**: la frontera del conocimiento externo. Contrato de tres estados
  (el mismo del streaming del engine): `Ready` (datos escritos), `Empty` (no
  existe) y `Pending` (lectura asíncrona no lista).
- **`RamBlockSource`**: fuente en RAM funcional (blob de bloques contiguos), base de
  tests y preload. Los backends de disquete Amiga (trackloader) y de sistema de
  archivos del PC aún no están implementados; se enchufan con el mismo contrato.
- **`BlockCache<BlockSize,Capacity>`**: caché LRU sobre `eng::util::LruCache`; la
  RAM es exactamente `Capacity*BlockSize`. Comprueba aciertos, fallos, expulsión del
  menos reciente, que un bloque ausente **no** se cachea y que una fuente inválida
  devuelve vacío.

## Salida de referencia

```
eng::board storage:
OK: storage (BlockSource 3 estados, cache LRU, ausentes e invalidos)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/146_board_storage
```
