# HOST-434 - `eng::graphics::build_sprite_dma_chain` (emisión DMA encadenada por canal)

Test host del builder de la **cadena DMA de un canal** (`eng/graphics/sprite_chain.hpp`,
F3 de `ROADMAP_SPRITES_COMPLETO.md` §3.2): la estructura `[POS,CTL,DAT/DATB…][POS,CTL,…]…[0,0]`
que Agnus recorre sola cuando un mismo canal sirve varios placements en líneas disjuntas
(ristras, multiplexado vertical). Es la alternativa al rearme por Copper (un `WAIT` +
4 MOVEs por reaparición): el canal se arma una vez por frame en la cabecera de la cadena.

Mecanismo contrastado con la fuente del emulador (`docs/reference/emulators/winuae/sprite-dma.md`):
el DMA lee `POS`/`CTL` de `SPRxPT` con `dmastate == 0` y los procesa como cabecera
(`../WinUAE-DBG/custom.cpp:4078-4090`), `sprstartstop` arranca/para por `VSTART`/`VSTOP`
(`custom.cpp:4031-4042`), el puntero avanza con cada fetch (`custom.cpp:12012-12023`) y el
canal ocioso sigue consumiendo words (`generate_sprites`, `custom.cpp:10055-10120`).

Cubre:

- `encode_sprite_pos_ctl`: POS (`VSTART[7:0]`, `HSTART[8:1]`) y CTL (VSTOP exclusivo,
  `ATTACH` bit 7, `VSTART[8]` bit 2, `VSTOP[8]` bit 1, `HSTART[0]` bit 0).
- **Golden de words**: 2 bloques con DATA conocida → secuencia exacta, terminador `0,0`,
  offsets de bloque (`block_pos_words`) y runway intacto (el builder no escribe tras el
  terminador).
- Sprite de **32 px** (`width_words = 2`) y `HSTART` con byte alto/bit bajo.
- **Rechazos sin escritura** (todo-o-nada): span vacío, storage vacía, bloque que abuta
  (gap < `kSpriteVerticalGapLines`), solape, canales mezclados, `ATTACH` en canal par,
  altura 0, ancho 0, DATA corta, capacidad y tope de bloques (`kSpriteChainMaxBlocks`).
- **Recorte de altura** a `kSpriteMaxHeightLines` (128 líneas).
- `patch_sprite_chain_block` (cadena persistente): cambia solo los 4 bytes de POS/CTL del
  bloque pedido, deja la DATA intacta y valida índice/altura/paridad de `ATTACH`.

Es freestanding (sin hardware, sin heap, sin STL). La storage es `ChipView<SpriteTag>`
(DMA tipada por banco, AGENTS §1.10); en host el banco es solo tipo.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/434_sprite_dma_chain   # solo este
bash tools/run-host-tests.sh                                          # todos
```

## Referencias

- `docs/reference/emulators/winuae/sprite-dma.md` (estructura DMA y columna fantasma).
- AHRM 3.ª, cap. 4 (formato `SPRxPOS`/`SPRxCTL`).
- `docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.2 (plan F3: builder, armado,
  cadena persistente y selector por canal).
- `engine/include/eng/graphics/sprite_chain.hpp`.
