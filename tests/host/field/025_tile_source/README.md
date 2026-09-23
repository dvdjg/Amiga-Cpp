# Test HOST-025: accesor de tiles `TileSource` (denso y disperso)

Respalda el concepto `eng::field::TileSource` y sus dos implementaciones
(`engine/include/eng/field/tile_source.hpp`, `tile_map.hpp`):

- `TileLayerMap`: mapa **denso** con wrap toroidal/acotado y `empty_tile`.
- `SparseTileMap<Chunk>`: mapa **disperso por chunks** (sólo regiones pobladas),
  compatible con los "mapas infinitos" de Tiled; los chunks ausentes devuelven
  `empty_tile`.

Se comprueba que ambas cumplen el concepto, la resolución de celdas (incluidas
coordenadas negativas y chunks ausentes) y la semántica que usa el scroll: los tiles
`empty_tile` **no se pintan**.

```bash
bash tools/run-host-tests.sh tests/host/field/025_tile_source
```

Contexto: `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2/§2.1.
