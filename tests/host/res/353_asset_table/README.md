# HOST-353 - asset_table

Test de `eng::res::AssetTable` (`eng/res/asset_table.hpp`).

## Qué cubre

Tabla **no propietaria** de blobs en memoria (por ejemplo, `incbin`): conserva `StringView` para el
nombre y `Span<const u8>` para los bytes. El caller debe mantener vivos ambos rangos mientras use la
tabla. `Assets` conserva el bloque dueño y vacía la tabla en `reset_phase`; una consulta inexistente
devuelve una vista vacía.

## Cómo se ejecuta

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/353_asset_table
```
