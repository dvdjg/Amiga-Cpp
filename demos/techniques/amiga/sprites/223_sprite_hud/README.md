# Demo 223: `sprite_hud` — HUD de texto por sprites rearmados por línea

HUD de **nombre + versión + fps** dibujado con **sprites rearmados por línea**
(`SpriteLineLayer`, HOST-418), no con escrituras al bitmap: la copperlist queda
**estática** tras el setup y el coste por frame del HUD es **cero** (el DMA de sprites la
ejecuta sola). El único valor dinámico es la estimación de fps: **≤1 actualización por
segundo** y solo cuando el número cambia.

## Mecánica (contra la fuente, `docs/reference/emulators/winuae/sprite-dma.md`)

- Cada canal se **arma una vez** por registro: `SPRxPOS` + `SPRxCTL` + **`SPRxPTH/L` → su
  tabla de DATA** (`[channel][line][2]`, patrón `arm_object`/053). El DMA hace fetch de 2
  palabras por línea y **avanza el walk** por la tabla: la DATA del HUD vive ahí, porque el
  registro `SPRxDATA` lo pisa el fetch.
- Por (línea, canal), el Copper hace una **reposición POS-only** (patrón Risky Woods, 1
  MOVE): el canal ya armado se redibuja más a la derecha en la misma línea.
- El primer canal empieza en x=32 (> fetch de la línea, DDFSTRT/2 ≈ 28 px) y la banda del
  HUD está en la franja inferior (y=232..255): la reconstrucción por cambio de fps
  (≤1/s, tras el VBlank) termina mucho antes de que el haz llegue allí.
- Coherencia: `display_planes=3` activa `setup::check_sprite_line_data` (≤5 planos).

## Estado: ABIERTA (no renderiza aún)

La emisión es la del patrón validado (volcado de la lista verificado en el emulador:
armados `POS/CTL/PT` correctos + reposiciones + DMACON ✓) pero los segmentos **no aparecen**
en pantalla (0 px del color del sprite en toda la captura, con las dos convenciones de
color probadas). Diagnóstico pendiente con la traza DMA del emulador
(`WinUAE-DBG`); mientras, la técnica viva del rearmado está en 053/207/217.

## Build & run

```bash
tools/build/build-demo.sh demos/techniques/amiga/sprites/223_sprite_hud --clean
tools/run/run-demo.sh       demos/techniques/amiga/sprites/223_sprite_hud
```
