# Test HOST-030: TileMapView (accesor de scroll con límites) y wrap_period

Respalda `eng::field::TileMapView<Src>` (`engine/include/eng/field/tile_source.hpp`): envuelve
un accesor `TileSource` (aquí un `StreamingWorldMap`) con los límites del mundo
(`width/height/wrap_x/wrap_y/edge_tile`) que necesita el scroll, de modo que el playfield no
dependa del almacén concreto (denso, disperso o streaming).

Se comprueban: `wrap_period` (potencia de dos con negativos, no-potencia y sin wrap), vista sin
`src` (sin datos, devuelve `edge_tile`), lectura no residente como `empty`, direccionamiento de
una celda residente, wrap en X con cotas negativas y por encima del ancho, borde en Y y
evicciones al superar la capacidad del pool (acceso **solo-residentes**).

```bash
bash tools/run-host-tests.sh tests/host/field/030_tile_map_view
```

Contexto: `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2. Demo que lo ejercita en
hardware: `demos/techniques/amiga/playfield/111_xlimited_sidescroller` (mundo `StreamingWorldMap` + `prefetch` de
la banda por frame).
