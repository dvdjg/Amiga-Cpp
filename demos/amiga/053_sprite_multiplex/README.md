# Demo 053: sprites hardware — multiplexado y color multiplexing

> **ESTADO: a revisar.** La demo valida el camino de sprites del engine y destapó
> (y corrigió) dos bugs reales en `SpriteManager`. El **reuso vertical** (rearmar
> un canal con varios segmentos, "chasing the raster") solo dibuja los últimos 2
> segmentos de 6: queda pendiente depurar el timing exacto del rearm del DMA de
> sprites en WinUAE-DBG. No es una demo final.

## Qué valida

- `SpriteTemplate` (plantilla portable: `segments` + `switches` de paleta).
- `SpriteManager::emit_template_into` (emite `SPRxPT/POS/CTL` + `COLORxx` en el
  Copper sin que el juego toque registros).
- `CopperScheduler` (composición de la copperlist con display + paleta + sprites).

## Bugs corregidos en el engine (destapados por esta demo)

1. **Offsets de registros de sprite** (`sprite_manager.hpp`): `emit_config`
   escribía `SPRxCTL/SPRxPOS` en `0x0d0/0x0d2` (¡registros de audio!), en vez de
   `0x142/0x140`. Sin esto no se dibujaba ningún sprite.
2. **Bit SH1** de `SPRxCTL`: se calculaba `(hpos >> 1) & 0x100`; ahora es
   `hpos & 0x100` (bit 8 real de HSTART).
3. Se añadió `DmaSprite` (`SPREN`, `0x0020`) a `copper.hpp` y el parámetro
   `hpos` a `emit_template_into`.

## Pendiente (rearm de sprites)

El rearm de un canal (`wait_line(line-1)` + reescribir `SPRxPT/POS/CTL`) dibuja
solo los últimos 2 de 6 segmentos. Es un problema de timing del DMA de sprites en
el emulador; el patrón exacto (línea del rearm vs. armado del DMA) requiere
depurar con watchpoints del Copper/sprite (ver `WINUAE-MONITOR-EXTENSIONS.md`).

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/053_sprite_multiplex --clean
tools/run/run-demo.sh       demos/amiga/053_sprite_multiplex
```
