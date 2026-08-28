# 102_tile_scroll_dualpf

Dual playfield con scroll independiente por playfield usando el driver generico
`TileScrollScene<Mode>` (`engine/include/eng/graphics/drivers/tile_scroll.hpp`).

La escena usa el modo dual **2+3** (`TileScrollMode::dual(2, 3)`): 5 bitplanes en
total, con 2 planos en el primer plano y 3 en el fondo.

- **Fondo** = PF1 (planos impares 1,3,5), 3 bitplanes, paleta `COLOR00..07`.
  Tiles con glifos hexadecimales y borde.
- **Primer plano** = PF2 (planos pares 2,4), 2 bitplanes, paleta `COLOR08..0B`.
  Tiles con un tramado del 50% de pixels (la mitad transparente) mas un glifo: el
  color 0 de PF2 es transparente, asi el fondo se ve a traves de los agujeros.

Cada playfield sigue un patron de movimiento distinto:

- el fondo deriva hacia la derecha y vuelve (`background_camera`);
- el primer plano deriva hacia la izquierda (opuesto) y bobea en vertical
  (`foreground_camera`).

Ambos comparten la misma superficie fisica de tiles (480x416) y el mismo fetch
(`DDFSTRT=$30`), pero cada uno programa su propio coarse (`BPLxPT`) y su propio
fine (`BPLCON1`: nibble bajo = PF1, alto = PF2). El prefetch por Blitter escribe
los tiles de cada playfield solo en sus planos (`make_playfield_upload_jobs`).

Esto demuestra que el mismo scroll (la misma logica de `TileScrollScene<Mode>`)
sirve para modos distintos sin tocar la logica de juego: la demo 101 usa la misma
clase en modo single 6 (EHB), y el test `tools/analyze/verify-tile-scroll-modes.mjs`
valida la descomposicion del scroll para 4/5/6 single y 2+3 / 3+3 dual.

Comandos:

```bash
bash ./tools/build/build-demo.sh demos/102_tile_scroll_dualpf --debug --clean
bash ./tools/run/run-demo.sh demos/102_tile_scroll_dualpf --sequence-frames 8 --sequence-interval-ms 150
bash ./demos/102_tile_scroll_dualpf/analyze-sequence.sh --warp
```

## API preparada para RoboCod

`TileScrollInput::plane[i]` permite anadir un offset coarse por bitplane a los
punteros `BPLxPT`, para el caso de un bitplane de fondo con scroll propio distinto
a los demas dentro de un playfield (la tecnica de RoboCod, conseguida por Blitter).
Esa demo sera la siguiente.
