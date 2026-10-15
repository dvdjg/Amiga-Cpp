# HOST-418 — `eng::graphics::SpriteLineLayer`

Test host de la **capa/HUD de sprites por parcheo de `SPRxPOS` + `SPRxDATA`/`DATB` por
línea** (`eng/graphics/sprite_line_layer.hpp`), el patrón *Parasol Stars* (un sprite para
todo el marcador) / *Brian the Lion* (imagen distinta por scanline).

Cubre:

- `configure`: rechaza rango de líneas, corrida de canales, `hpos_step == 0` y tramos que
  se salen de los 8 canales.
- **Emisión** (rearmado por línea): un armado por canal (`POS`+`CTL`+`PT` con stride 4,
  `0x120+ch*4`) y una **reposición** POS-only por (línea, canal), en orden línea→canal.
- **Armado único** (`per_line_rearm=false`): solo `POS`+`CTL`+`PT` por canal, sin
  reposiciones — la forma para bandas en `vpos > 255`, donde el `WAIT` del Copper no puede
  apuntar (`custom.cpp:4427`) y el secuenciador auto-arma en `VSTART`
  (`custom.cpp:10088-10090`).
- **DATA por línea** (tabla `[channel][line][2]`) — la estructura que recibe `SPRxPT`.
- **Scroll** horizontal (resta a la X, omite los canales fuera de pantalla) y **attach**.
- Límites de hardware coherentes en `eng/graphics/sprite_limits.hpp`.

Es freestanding (sin hardware, sin heap, sin STL): el scheduler se sustituye por un espía
que guarda la lista de rearmes. El `main.cpp` usa `printf` solo para informar.

> Nota: no valida la **carrera contra el haz** (eso lo decide el `copper::Scheduler` con la
> `Timeline`); valida la geometría/orden que el driver entrega.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/418_sprite_line_layer   # solo este
bash tools/run-host-tests.sh                                            # todos
```
