# API de campos de tiles

`TileFieldController` es un scheduler portable para campos planares. No conoce
DPF ni escribe registros del Amiga. Reserva memoria mediante `MemorySystem`,

## Geometría

Para un bloque de `BW x BH` y viewport `VW x VH`:

```text
surface_w = scroll_x ? 2*VW + 2*BW : VW
surface_h = scroll_y ? 2*VH + 2*BH : VH
```

Un eje que no scrollea no reserva margen en el otro eje. La ventana parte en
`(VW, VH)` cuando ambos ejes están activos. La unidad de trabajo es siempre un
bloque de tiles, no una página lineal de viewport.

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
- `pump(plan, budget)`: consume bandas en pasos de tiles sin heap, excepciones ni
  RTTI.
- `hardware_view(first_plane)`: calcula puntero coarse, fine scroll y módulo.

Cada job tiene destino fuera de la ventana visible. En diagonal la banda X es
propietaria de la esquina y la banda Y se recorta una celda, por lo que no hay
duplicados ni trabajos para filas o columnas opuestas.

## Blitter

`TileBlockCopy` usa tiles de anchura múltiplo de 16. Un tile rectangular de
`32x16`, `16x32` o `32x32` se representa con `words_per_row = width/16` y
`height = tile_height`. Cuando el layout del tileset permite tiles contiguos,
el scheduler puede fusionarlos en un rectángulo; si no, conserva jobs por tile.
Esto minimiza jobs lógicos sin asumir datos no contiguos.

El backend OCS no tiene una operación multi-plano para este contrato: arranca
una operación física del Blitter por plano de cada job. El scheduler, por tanto,
fusiona regiones compatibles a nivel lógico, pero no inventa un blit multi-plano.

## Hardware y límites

Con `DDFSTRT=$30`, el fetch es 40 bytes visibles más 2 bytes de margen. La
fórmula usada es `fetch=(scroll-1)&~15`, `BPLCON1=(16-fine)&15` y
`BPL1MOD=row_bytes-42`. Si el fine scroll deja ruido en los primeros 16 píxeles,
la configuración visual puede ocultar esa banda; no se corrige escribiendo en
la ventana visible.

La API no implementa clipping de mundo no periódico más allá de `edge_tile`, ni
tiles con anchura no alineada a palabra. El presupuesto de jobs y tiles sigue
siendo responsabilidad de `FramePlan` y de la aplicación.

## Showcase y verificación

`demos/106_tile_field_showcase` cubre dual 3+3 y single de 5 planos. Las
variantes `K_TILE_WIDTH=16/32` y `K_DUAL=0` usan el mismo controlador.
`tools/analyze/verify-tile-field-fill.mjs` prueba las cuatro geometrías, X/Y/XY,
deltas 1..5, ambos sentidos, cruces, esquinas, recentrado y ejes únicos.
