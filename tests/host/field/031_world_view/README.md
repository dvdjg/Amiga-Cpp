# Test HOST-031: WorldView (chunk WorldMap sobre UAF-R)

Respalda `eng::assets::WorldView` (`engine/include/eng/assets/uaf.hpp`), la vista del chunk
`WorldMap` definido en `docs/engine/architecture/WORLD_FORMAT.md`: mapa de tiles en chunks con
`gid` ya convertido a índice de banco por el pipeline host.

Se comprueban: cabecera y geometría de chunk, descriptores de capa, directorio ordenado con
búsqueda binaria (`find_chunk`), lectura de celdas, **cruce de chunk**, **wrap toroidal**,
**chunks ausentes** y celdas fuera de `width`/`height` como `empty_tile`, metadatos por capa y
el rechazo de payload corto, versión inválida y bloques fuera de rango.

```bash
bash tools/run-host-tests.sh tests/host/031_world_view
```

Contexto: `docs/engine/architecture/WORLD_FORMAT.md`,
`docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2.
