# HOST-416 — `eng::graphics::SpriteChannelLedger` / `plan_sprite_windows`

Test host del **reparto de canales de sprite por ventanas de reprogramación** (backbone puro de
`docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`). Valida reservar canales para un **fondo por
sprites** en un intervalo y repartir los **objetos** solo en los canales libres de ese intervalo,
recuperando los 8 canales por encima y por debajo. Cada canal se reprograma de forma independiente,
así que dos ventanas pueden solaparse en vertical si usan canales distintos.

Cubre:

- `SpriteChannelLedger`: `occupy`/`free` (solape, bordes exclusivos), corridas
  (`occupy_run`/`free_run`) y máscara por línea (`free_mask`).
- `plan_sprite_windows`: ventanas válidas y errores (`BadRange`, `BadChannels`, `ChannelBusy`,
  alineación de `attach`); el **solape vertical con canales disjuntos es válido**.
- **Híbrido**: una ventana *Risky Woods* `[60,100)` en los canales 0..5 deja 6 y 7 para
  objetos de ese intervalo (el 3.º degrada a BOB); arriba y abajo se recuperan los 8 canales.
- **Attached y tiras** respetan el ledger.
- **Compatibilidad**: un ledger vacío reproduce el reparto clásico de 8 canales.

Es freestanding (sin hardware, sin heap): se compila con `g++` del host y corre como
binario nativo. El `main.cpp` usa `printf` solo para informar.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/416_sprite_channel_window   # solo este
bash tools/run-host-tests.sh                                    # todos
```
