# Test HOST-026: cache de chunks residentes (streaming bajo presupuesto)

Respalda `eng::field::ChunkCache<ChunkSize, Capacity>`
(`engine/include/eng/field/chunk_cache.hpp`): mantiene `Capacity` chunks en un pool
aportado por el llamador, los carga bajo demanda con un `Loader` y evicta por **LRU**.

Es la pieza que hace viable un `WorldMap` disperso sin tener todo el mundo en Chip
RAM: el pool entra en el modelo de ocupación (`SCENE_AND_RESOURCES.md`).

Se comprueban: init válido/inválido, carga y datos correctos, hit devuelve el mismo
puntero, y la política de evicción LRU.

```bash
bash tools/run-host-tests.sh tests/host/field/026_chunk_cache
```

Contexto: `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2.
