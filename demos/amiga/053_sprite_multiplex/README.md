# Demo 053: sprites hardware — multiplexado y color multiplexing

Valida el camino de sprites de la nueva estructura del engine: `SpriteTemplate`
(plantilla portable con segmentos y cambios de paleta) + `SpriteManager`
(`emit_template_into`), que reusa UN canal de sprite hardware para dibujar SEIS
objetos distintos en franjas verticales distintas ("chasing the raster"), cada uno
con su propio color (`SpritePaletteSwitch` cambia `COLORxx` por franja).

Qué muestra: seis barras de 16 px (anchura decreciente, a modo de pirámide), cada
una de un tono saturado distinto (rojo, verde, azul, amarillo, cian, magenta),
apiladas verticalmente y servidas por el canal 0. En `update()` el bloque entero
**rebota horizontalmente** (onda triangular, 40..229 px) reconstruyendo la
copperlist por frame, para mostrar el reposicionado por Copper en movimiento.

## Lo que esta demo destapó y corrigió

- **Codificación de `SPRxPOS`/`SPRxCTL` invertida** (`sprite_manager.hpp`): la
  posición horizontal iba en el byte alto y la vertical en el bajo; el correcto es
  `SPRxPOS = (VSTART << 8) | (HSTART >> 1)` y `SPRxCTL = (VSTOP << 8) | control`
  (HSTART en low-res px ÷ 2; ver `amiga-bootcamp/08_graphics/sprites.md` y AHRM
  cap. 5). Sin esto no se dibujaba ningún sprite en su sitio.
- **Offsets de registros de sprite** (`0x0d0/0x0d2` → `0x142/0x140`): los antiguos
  caían en registros de audio.
- **`DmaSprite` (`SPREN`, `0x0020`)** añadido a `copper.hpp`; el parámetro `hpos`
  a `emit_template_into`.
- **Rearm**: `WAIT` en la línea VSTART de cada segmento (patrón del bootcamp), con
  un gap de 1 línea entre instancias.

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/053_sprite_multiplex --clean
tools/run/run-demo.sh       demos/amiga/053_sprite_multiplex
```
