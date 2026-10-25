# HOST-498 — MapBundle v1 / MapAssetView / LayerView

Valida el primer runtime de contenido Tiled: la cabecera big-endian del `MapBundle` v1, la tabla de capas, las celdas `u16` BE, la paleta en palabras Amiga y el adaptador `eng::map::LayerView` que cumple `playfield::TileMap` (wrap X toroidal, Y acotado y centinela de vacío).

El bundle de prueba es de 88 B (40 B de cabecera + 32 B de `LayerDesc` + 8 B de paleta de 4 colores + 4 celdas de 2×2) y está embebido en el test; no depende de assets externos. El caso negativo cambia el magic y comprueba que `MapAssetView` y `LayerView` rechazan el bundle sin acceder a datos fuera del span.
