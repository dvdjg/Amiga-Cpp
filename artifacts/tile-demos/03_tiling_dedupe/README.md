# 03 Ã‚Â· Tiling + dedupe del tilebank (mapa "Beginning Fields")

Fuente: atlas 640Ãƒâ€”640 = 40Ãƒâ€”40 tiles de 16Ãƒâ€”16 (The Fan-tasy Tileset).

El slicer convierte cada tile de 16Ãƒâ€”16 en un **ÃƒÂ­ndice de banco**. El dedupe
exacto une todas las celdas idÃƒÂ©nticas a la MISMA entrada del tilebank, y
`kTileIndexedMap[]` guarda, por celda, quÃƒÂ© tile (ÃƒÂ­ndice) hay que pintar.

- `dedupe_exacto`: ; (original cuantizado vs reconstruido): 100.00% de 409600 índices (reconstrucciÃƒÂ³n idÃƒÂ©ntica sin fusiÃƒÂ³n: `reconstruct.png` = original cuantizado).
- `dedupe_merge095`: con `--merge 0.95` se fusionan tiles casi iguales (fracciÃƒÂ³n de ÃƒÂ­ndices Ã¢â€°Â¥ 0.95) para reducir el banco a costa de pÃƒÂ©rdida permitida.

`tilebank.png` es la hoja de tiles ÃƒÂºnicos; `reconstruct.png` es lo que se ve en
el Amiga al dibujar `banco[kTileIndexedMap]`.
