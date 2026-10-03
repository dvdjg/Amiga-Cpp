# Referencia — `framebuffer` (`eng/api/framebuffer.hpp`)

`eng::IndexedDisplay<Planes, Buffers>` es el **framebuffer indexado** de nivel A: el juego escribe
**índices** (1 byte/píxel, `0..2^Planes-1`) en un buffer lineal y llama `present()`; el engine hace la
conversión **chunky→planar (C2P) por Blitter** y publica en VBlank. El juego no ve planos, `Scene` ni
Copper. Es el camino de un **efecto por píxel** (o *fallback* de un consumidor que no mapea a tiles).

## Métodos

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `init` | `bool init(Backend&, u16 width, u16 height, PaletteWords palette, u8 colors, u8 row_repeat = 1)` | backend, geometría, paleta, colores, `row_repeat` (muestra cada fila N veces) | `false` si no cabe o la config no vale. |
| `framebuffer` | `Span<u8> framebuffer()` | — | el buffer chunky del frame trasero (`px[y*width()+x]`). |
| `present` | `void present()` | — | — (C2P + swap de copperlist). Llamar una vez por frame. |
| `width`/`height` | `u16 …() const` | — | ancho / alto de pantalla. |
| `rows` | `u16 rows() const` | — | filas del **framebuffer** (`height / row_repeat`). |
| `planes`/`buffer_count` | `u8 …() const` | — | profundidad / buffers. |
| `scene` | `Scene& scene()` | — | la escena poseída (efectos avanzados). |

## Parámetros y estado

- `Planes` 4..6 (16/32/64 colores). `Buffers` 1..3 (doble/triple para no desgarrar).
- `row_repeat > 1` **reduce el fill por frame** (`1/row_repeat`), repitiendo filas por Copper (truco de
  las demos 061/080).
- **Estado**: el camino de **4 planos** está verificado (demo `061_indexed_display`); el C2P de 5/6
  planos está verificado en host (`HOST-367`), pero su **display** aún no renderiza en hardware.

```cpp
eng::IndexedDisplay<4u, 2u> fb;
fb.init(backend, 256, 240, palette, 16u, 2u);      // 256x240, 16 colores, row_repeat=2
// update: fb.framebuffer()[y*fb.width() + x] = indice;
// render: fb.present();
```

> Fuente: `engine/include/eng/api/framebuffer.hpp`. C2P: `eng/graphics/c2p.hpp` +
> `docs/engine/architecture/C2P_BLITTER.md`. Demo: `demos/features/engine/amiga/061_indexed_display`.
>
> **Nota**: para un motor de tiles/sprites (p. ej. un emulador NES), **no** uses este camino: emula a
> nivel de tile/sprite — ver `docs/engine/NES_CONSUMER_GUIDE.md` §6.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
