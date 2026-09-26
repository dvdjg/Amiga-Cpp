# HOST-353 - asset_table

Test de `eng::res::AssetTable` (`eng/res/asset_table.hpp`).

## Qué cubre

Tabla **declarativa de assets en memoria** (blobs `incbin`): se registran por **nombre** y se piden
con su **dominio** (`get<PlaneTag>("abyss") -> ByteView<PlaneTag>`), sin tamaños ni punteros en la
lógica. Capacidad fija (sin heap) y búsqueda por nombre; una consulta inexistente devuelve una
vista vacía.

## Cómo se ejecuta

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/353_asset_table
```
