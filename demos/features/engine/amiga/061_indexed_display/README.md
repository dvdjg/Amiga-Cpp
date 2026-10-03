# Demo 061 — `eng::IndexedDisplay`: framebuffer indexado de nivel A

Tutorial de la **fachada de framebuffer** (`eng/api/framebuffer.hpp`): el juego escribe **índices**
(1 byte/píxel, 0..15) en un buffer **lineal** y el engine hace la conversión **chunky→planar (C2P) por
Blitter** a los bitplanes y la **publica en VBlank**. El juego **no ve** planos (`BPLxPT`), `Scene`,
módulos ni Copper — solo índices.

```cpp
eng::IndexedDisplay<4, 2> fb;               // 4 planos (16 colores), doble buffer
fb.init(backend, 256, 240, palette, 16);    // reserva escena + buffers chunky
// update: fb.framebuffer() -> Span<u8>, índice por píxel (fila a fila)
// render: fb.present()                        -> C2P + swap de copperlist
```

Es el **nivel A** del camino del emulador NES (`docs/engine/NES_CONSUMER_GUIDE.md` §1.2): resolución
**256×240**, 4 planos (16 colores), doble buffer. La demo dibuja barras horizontales que ciclan de
color + una barra vertical que barre la pantalla. El nivel B equivalente (C2P + `Scene` a mano) está
en `demos/techniques/amiga/c2p/061_c2p_chunky_4bpl`.

## Build / run

```bash
bash ./tools/build/build-demo.sh demos/features/engine/amiga/061_indexed_display --release
bash ./tools/run/run-demo.sh demos/features/engine/amiga/061_indexed_display
```
