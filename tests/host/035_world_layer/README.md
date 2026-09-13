# Test HOST-035: WorldView → scroll (WorldLayerSource + Loader-RAM)

Respalda `eng::field::WorldLayerSource` y `eng::field::WorldMapChunkLoader`
(`engine/include/eng/field/world_layer.hpp`): los puentes entre un mundo empaquetado
(`eng::assets::WorldView`, chunk `WorldMap`) y el motor de scroll.

Se comprueba: la capa como `TileMap` directo (bounds, celdas, cruce de chunk, borde), y la cadena
**WorldMap → `StreamingWorldMap` (Loader-RAM) → `TileMapView`**: acceso solo-residentes, carga al
`prefetch`, equivalencia con el acceso directo en un barrido, y chunk ausente → `Empty`.

```bash
bash tools/run-host-tests.sh tests/host/035_world_layer
```

Contexto: `docs/engine/architecture/WORLD_FORMAT.md`,
`docs/engine/architecture/STREAMING_LOADER.md`.
