# API de campos de tiles

`TileFieldController` es un scheduler portable para campos planares. No conoce
DPF ni escribe registros del Amiga. Reserva memoria mediante `MemorySystem`,

## Geometría

Para un bloque de `BW x BH`, viewport `VW x VH` y `margin_blocks`:

```text
surface_w = scroll_x ? VW + margin_blocks*BW : VW
surface_h = scroll_y ? floor((VH + margin_blocks*BH)/BH)*BH + 1 : VH
```

Un eje que no scrollea no reserva margen en el otro eje. La ventana parte en
`(VW, VH)` cuando ambos ejes están activos. La unidad de trabajo es siempre un
bloque de tiles, no una página lineal de viewport.

`margin_blocks` es explícito, admite 2 o 3 y vale 2 por defecto. Es el margen total:
con 2 hay un bloque de seguridad a cada lado; 3 es opcional para dar más tolerancia
a 8-way, velocidad o inversiones, no un requisito geométrico.

La superficie física se interpreta como circular/recentrable. La cámara lógica
(`world_x/world_y`) es independiente de `window_x/window_y`. Cuando la ventana
llega a una frontera, se selecciona la copia del lado opuesto y se modifica el
origen lógico del mundo. El contenido coincidente de ambos lados evita copiar
la pantalla.

## API

- `begin(memory, config, offset)`: reserva la superficie y programa un único
  rellenado inicial. La aplicación debe llamar `pump` hasta `busy() == false`
  antes de mostrar el primer frame.
- `update(config, delta, plan)`: acepta delta relativo, lo limita a `[-5,5]`,
  detecta cruces de bloque en X, Y o XY y programa solamente las bandas nuevas.
- El resultado indica `accepted`, `applied` y `pending`; los deltas recibidos
  mientras el Blitter está ocupado se acumulan, no se pierden.
- `pump(plan, budget)`: consume bandas en pasos de tiles sin heap, excepciones ni
  RTTI.
- `hardware_view(first_plane)`: calcula puntero coarse, fine scroll, módulo y
  metadata de guardia y split; la firma existente no cambia.

Cada job tiene destino fuera de la ventana visible. En diagonal la banda X es
propietaria de la esquina y la banda Y se recorta una celda, por lo que no hay
duplicados ni trabajos para filas o columnas opuestas.

La línea `surface_h - 1` es `guard_line` cuando `scroll_y` está activo. No forma
parte de `tile_rows`, no se rellena con una celda del mapa y se inicializa a cero.
Para 320x256, tiles 16x16 y margen 2, la geometría es `352x289`, con `18` filas
de tiles y guardia en la línea `288`.

## Blitter

`TileBlockCopy` usa tiles de anchura múltiplo de 16. El layout por defecto es
`TileMajor` (`[tile][plane][row][word]`), que no permite fusionar tiles adyacentes
arbitrarios. Solo layouts `RowMajor`/`ColumnMajor` con stride contratado pueden
fusionar filas/columnas; el caso genérico conserva un job por tile. Un tile rectangular de
`32x16`, `16x32` o `32x32` se representa con `words_per_row = width/16` y
`height = tile_height`. Cuando el layout del tileset permite tiles contiguos,
el scheduler puede fusionarlos en un rectángulo; si no, conserva jobs por tile.
Esto minimiza jobs lógicos sin asumir datos no contiguos.

El backend OCS no tiene una operación multi-plano para este contrato: arranca
una operación física del Blitter por plano de cada job. El scheduler, por tanto,
fusiona regiones compatibles a nivel lógico, pero no inventa un blit multi-plano.

## Hardware y límites

Con `DDFSTRT=$30`, el fetch horizontal es 40 bytes visibles más 2 bytes de margen.
fórmula usada es `fetch=(scroll-1)&~15`, `BPLCON1=(16-fine)&15` y
`BPL1MOD=row_bytes-42`. El margen de fetch es adicional al margen lógico y debe
comprobarse contra el ancho físico. Verticalmente no hay fine scroll hardware:
se ajusta el puntero por scanline y el módulo avanza al siguiente `row_bytes`. Si el fine scroll deja ruido en los primeros 16 píxeles,
la configuración visual puede ocultar esa banda; no se corrige escribiendo en
la ventana visible. El compositor Copper espera en `DIWSTRT + split_line` y
emite un segundo conjunto de `BPLxPT` para el tramo inferior.

La API no implementa clipping de mundo no periódico más allá de `edge_tile`, ni
tiles con anchura no alineada a palabra. El presupuesto de jobs y tiles sigue
siendo responsabilidad de `FramePlan` y de la aplicación.

## Showcase y verificación

`demos/106_tile_field_showcase` cubre dual 3+3 y single de 5 planos. Las
variantes `K_TILE_WIDTH=16/32` y `K_DUAL=0` usan el mismo controlador.
`tools/analyze/verify-tile-field-fill.mjs` prueba las cuatro geometrías, X/Y/XY,
deltas 1..5, ambos sentidos, cruces, esquinas, recentrado y ejes únicos.
