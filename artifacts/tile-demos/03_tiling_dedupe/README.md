# 03 · Tiling + dedupe del tilebank (mapa "Beginning Fields")

Fuente: atlas 640×640 = 40×40 tiles de 16×16 (The Fan-tasy Tileset).

El slicer convierte cada tile de 16×16 en un **índice de banco**. El dedupe
exacto une todas las celdas idénticas a la MISMA entrada del tilebank, y
`kTileIndexedMap[]` guarda, por celda, qué tile (índice) hay que pintar.

- `dedupe_exacto`: ; (original cuantizado vs reconstruido): 100.00% de 409600 índices (reconstrucción idéntica sin fusión: `reconstruct.png` = original cuantizado).
- `dedupe_merge095`: con `--merge 0.95` se fusionan tiles casi iguales (fracción de índices ≥ 0.95) para reducir el banco a costa de pérdida permitida.

`tilebank.png` es la hoja de tiles únicos; `reconstruct.png` es lo que se ve en
el Amiga al dibujar `banco[kTileIndexedMap]`.
