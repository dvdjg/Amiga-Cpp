# Demo 223: `sprite_hud` — HUD de texto por sprites (armado único, banda inferior)

HUD de **nombre + versión + fps** dibujado con **sprites DMA** (`SpriteLineLayer`, HOST-418),
no con escrituras al bitmap: la copperlist queda **estática** tras el setup y el coste por
frame del HUD es **cero** (el DMA de sprites la ejecuta sola). El único valor dinámico es la
estimación de fps: **≤1 actualización por segundo** y solo cuando el número cambia.

## Mecánica (contra la fuente, `docs/reference/emulators/winuae/sprite-dma.md`)

- Cada canal se **arma una vez** por registro: `SPRxPOS` + `SPRxCTL` + **`SPRxPTH/L` → su
  tabla de DATA** (`[channel][line][2]`, patrón `arm_object`/053). El DMA hace fetch de 2
  palabras por línea y **avanza el walk** por la tabla: la DATA del HUD vive ahí, porque el
  registro `SPRxDATA` lo pisa el fetch. El bloque de punteros usa **stride 4**
  (`SPR0PTH=0x120`, `SPR1PTH=0x124`; `custom.cpp:7462-7464`); `POS`/`CTL` usan stride 8.
- La banda está en la **franja inferior** (`vpos 276..299` = `DIWSTRT 0x2C` + filas
  232..255). El armado se escribe **antes** de la banda (con el par de overflow de
  `wait_line_pal` para vpos ≥ 256, `copper.hpp:225-236`) y el **secuenciador de sprites
  auto-arma** cada canal al llegar a `VSTART` (`custom.cpp:10088-10090`). La **reposición**
  POS-only del rearmado por línea no expresa vpos ≥ 256 (`vline: u8`), así que se usa
  **armado único** (`per_line_rearm=false`) con canales disjuntos (3+3+2 = 8).
- El primer canal empieza en **x de pantalla 32** (> fetch de la línea, DDFSTRT/2 ≈ 28 px);
  la X de la API va en **HSTART absoluto** (pantalla = HSTART − 0x81, `drawing.cpp:2706`).
- El píxel de sprite vale `DATB*2 + DAT` (AHRM cap. 4): el bit del glifo va en **DAT** para
  color 1 (`COLOR17` = blanco).
- Coherencia: `display_planes=3` activa `setup::check_sprite_line_data` (≤5 planos).

## Estado: VERIFICADA

- **49.87 fps** emulados / 49.91 host, 142.244 ciclos/frame (1.003 campos) — coste del HUD
  por frame = 0 (copperlist estática; solo la actualización ≤1/s de los fps reconstruye).
- Render: "223 HUD" / "v1.0.NNNN" / "FPS nn" en blanco en la franja inferior (visión +
  recuento de píxeles blancos: 336 px en la zona inferior). Medidas y prompts en
  `VALIDATION.md`.
- Regresión host: HOST-418 cubre el armado único (`per_line_rearm=false`) y el rearmado por
  línea; el fix del stride de `SPRxPT` (0x120+ch*4) se cazó con esta demo.

## Build & run

```bash
tools/build/build-demo.sh demos/techniques/amiga/sprites/223_sprite_hud --clean
tools/run/run-demo.sh       demos/techniques/amiga/sprites/223_sprite_hud
```
