# HOST-089: rearmado horizontal de sprite

Test host de `Scheduler::emit_sprite_horizontal_rearm(s)` (`scheduler.hpp`) y del tipo
`graphics::SpriteHorizontalRearm` (`raster_intent.hpp`): el **multiplexado horizontal** de
un canal de sprite, la técnica de los fondos continuos tipo **Risky Woods** / **Free Form**
(ver `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`).

## Qué comprueba

1. **Codificación AHRM** de `SPRxPOS` (`(VSTART[7:0]<<8) | (HSTART[8:1])`) y `SPRxCTL`
   (`(VSTOP[7:0]<<8) | VSTART[8]<<3 | VSTOP[8]<<2 | HSTART[0]<<1 | attach`).
2. La secuencia emitida es **WAIT(posición) + POS + CTL + DATA + DATB** (4 MOVEs), y
   **NO toca `SPRxPT`** (reasignar el puntero relanzaría la secuencia DMA; en modo manual se
   escriben los registros directos, AHRM cap. 4 "Manual Mode").
3. El **WAIT** espera a una **posición H concreta** (no solo a la línea), que es lo que
   permite el rearmado dentro de la misma línea.
4. Los **offsets de registro** son los del canal: `SPRxPOS=0x140+ch*8`, `SPRxCTL=0x142+ch*8`,
   `SPRxDATA=0x144+ch*8`, `SPRxDATB=0x146+ch*8`.
5. La **variante de lista** (`emit_sprite_horizontal_rearms`) respeta el orden por `hpos` e
   **ignora los rearms atrasados** (un WAIT ya pasado esperaría al frame siguiente).

Todo con un `MemorySystem` sobre un buffer estático: sin hardware.

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/073_sprite_hrearm
```
