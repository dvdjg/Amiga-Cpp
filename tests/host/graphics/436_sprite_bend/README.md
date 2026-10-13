# HOST-436 - `drivers::emit_sprite_bend_into` (bending por línea)

Test host del driver de **bending por línea** (`eng/graphics/drivers/sprite_bend.hpp`, F4.2):
por cada línea del sprite se emite `WAIT` + `SPRxPOS`/`SPRxCTL` con `VSTART = línea actual`
(el registro rearma el DMA: `../WinUAE-DBG/custom.cpp:4078-4090`) y la X de una **tabla de
offsets** potencia de dos (máscara, sin `%`). El `SPRxPT` no se toca (lo dejó el armado del
objeto); la DATA debe traer terminador `0,0` (anti-*Phantom Sprite*,
`docs/reference/emulators/winuae/sprite-dma.md`).

Cubre:

- Una línea = un `WAIT` + `POS`/`CTL`; X = `x_base + offsets[(phase+i)&mask]`; `VSTOP` = fin exclusivo.
- `phase` desplaza el índice con máscara; `step = 2` reusa el offset dos líneas.
- X recortada a `>= 0`; rechazos sin emisión (canal inválido, tabla no potencia de dos,
  tabla trivial, altura 0, DATA sin filas+terminador); altura recortada a 128 líneas.
- **Varios canales intercalados por línea** (un solo `WAIT` compartido; `SPR2POS` antes que
  `SPR3POS` en la misma línea): emitirlos por bloques separados desordenaría los `WAIT`
  (hallazgo de la demo 220).

El scheduler se sustituye por el real ligado a un bloque Chip del arena (como HOST-428).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/436_sprite_bend   # solo este
bash tools/run-host-tests.sh                                       # todos
```

## Referencias

- Demo `demos/techniques/amiga/sprites/220_sprite_bend` (validada con visión y fps).
- `docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.3 (F4.2).
