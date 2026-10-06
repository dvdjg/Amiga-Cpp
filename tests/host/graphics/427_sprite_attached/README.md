# HOST-427 - `eng::graphics::cook_attached_pair` (par *attached* de 15 colores)

Test host del cocinado de la **DATA de un par de sprites *attached*** (`eng/graphics/sprite_attached.hpp`):
convierte un bitmap planar de **4 planos y 16 px** en los dos streams `DAT`/`DATB` que consumen
los canales del par (el canal par aporta los bits 0-1 del índice de color y el impar los bits
2-3; 0 = transparente, 1..15 = `COLOR17..31`), con el **terminador de DMA** obligatorio.

El bit `ATTACH` (bit 7 de `SPRxCTL`, solo en el canal impar) y la igualdad de posición de los
dos canales los emite `SpriteConfig`/`SpriteManager`; este módulo cubre el hueco declarado en
`docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md` §6 («falta un helper que cocine la DATA de
4 planos»).

Cubre:

- `attached_stream_words`: `height*2 + 2` words por canal (DAT/DATB por línea + terminador).
- **Rechazos** sin escritura: altura 0, `Span` de fuente/salida cortos, punteros nulos.
- **Round-trip por píxel**: el índice de 4 bits reconstruido del par coincide con el del bitmap
  de 4 planos (bit 15 = píxel 0, a la izquierda).
- **Mapeo de planos**: planos 0-1 → canal par; planos 2-3 → canal impar; terminadores a cero.

Es freestanding (sin hardware, sin heap, sin STL). El `main.cpp` usa `printf` solo para informar.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/427_sprite_attached   # solo este
bash tools/run-host-tests.sh                                          # todos
```

## Referencias

- AHRM 3.ª, cap. 4 («Attached Sprites», Table 4-5).
- `docs/reference/amiga/techniques/sprite-layer.md` §4 (estado del subsistema).
- `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md` §6.
- Demo `demos/techniques/amiga/sprites/214_attached_object`.
