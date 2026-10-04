# HOST-416 — `eng::graphics::SpriteChannelLedger` / `plan_sprite_bands`

Test host del **reparto híbrido de canales de sprite por franjas** (backbone puro de
`docs/engine/architecture/SPRITE_BANDS.md`). Valida reservar canales para un **fondo po
sprites** en una banda de raster y repartir los **objetos** solo en los canales libres de
esa franja, recuperando los 8 canales por encima y por debajo.

Cubre:

- `SpriteChannelLedger`: `occupy`/`free` (solape, bordes exclusivos), corridas
  (`occupy_run`/`free_run`) y máscara por línea (`free_mask`).
- `plan_sprite_bands`: bandas válidas y errores (`BadRange`, `BadChannels`, `Overlap`,
  `ChannelBusy`, alineación de `attach`).
- **Híbrido**: un fondo *Risky Woods* `[60,100)` en los canales 0..5 deja 6 y 7 para
  objetos de esa franja (el 3.º degrada a BOB); arriba y abajo se recuperan los 8 canales.
- **Attached y tiras** respetan el ledger.
- **Compatibilidad**: un ledger vacío reproduce el reparto clásico de 8 canales.

Es freestanding (sin hardware, sin heap): se compila con `g++` del host y corre como
binario nativo. El `main.cpp` usa `printf` solo para informar.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/416_sprite_band   # solo este
bash tools/run-host-tests.sh                                    # todos
```
