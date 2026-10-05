# Referencia — `Screen` (`eng/api/screen.hpp`)

`eng::Screen` es el **contexto de dibujo** de la fachada (el «`RastPort`» del engine): envuelve un
`playfield::DrawTarget` (superficie + rasterizador + plan + clip) y ofrece primitivas sin que el juego
vea planos, `FramePlan` ni `Rasterizer`. Se obtiene con `app.screen()`.

## Ciclo y consulta

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| ctor | `Screen(DrawTarget target, BlitStream stream = {})` | `target`, `stream` (lo inyecta el `App`) | — |
| `valid` | `bool valid() const` | — | si hay destino de dibujo. |
| `bounds` | `Box bounds() const` | — | el rect visible (clip). |
| `target` | `DrawTarget& target()` | — | el objetivo subyacente (**escape** para efectos; el juego normal no lo usa). |

## Primitivas inmediatas (rasterizador / CPU)

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `clear` | `void clear(u8 color)` | `color` (índice de paleta) | — (borra todo el área). |
| `fill` | `bool fill(Box, u8 color)` | `box`, `color` | `false` si no cabe/clip. **Inmediato** (CPU): puede pisar lo dibujado después. |
| `frame` | `bool frame(Box, u8 color)` | `box`, `color` | `false` si no cabe. |
| `line` | `bool line(x0,y0,x1,y1, u8 color, RasterOp op = Copy)` | extremos, color, operación | `false` si no cabe. |
| `text` | `bool text(x, y, const char* s, u8 color)` | posición, cadena, color | `false` si no cabe. |

## Primitivas diferidas (Blitter, encoladas en el plan del frame)

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `clear_box` | `bool clear_box(Box)` | `box` | `false` si no hay plan. Borra `D=0` **en orden** con los sprites. |
| `fill_box` | `bool fill_box(Box, u8 color)` | `box`, `color` | `false` si no hay plan. Relleno a **coste cero** diferido (`D=(A&B)\|(~A&C)`, `$CA`); preserva el borde aunque `x`/`w` no sean múltiplos de 16. |
| `sprite` | `bool sprite(const Sprite&, s16 x, s16 y, u8 frame = 0)` | sprite, posición, frame | `false` si no hay plan o el sprite no vale. |
| `erase_sprite` | `bool erase_sprite(const Sprite&, s16 x, s16 y)` | sprite, posición | `false` si no hay plan. |
| `bobs` | `u16 bobs(const BobLayer&, u8 fine_scroll = 0)` | capa de BOBs, fino del campo | nº de actores dibujados. |
| `blit` | `bool blit(src, x,y,w,h, src_row_bytes, src_plane_stride, planes[, shift, desc, op])` | geometría planar de origen | `false` si no hay plan. Para primitivas que `Screen` no cubre. |
| `bitmap` | `bool bitmap(ChipBitmapView<PlaneTag>, Box)` | asset planar + destino | `false` si geometría/layout no encajan. |
| `c2p` | `bool c2p(const C2pRequest&)` | petición chunky→planar | `false` si no cabe el plan. |
| `notify` | `bool notify(u16 ticket)` | `ticket` | `false` si no hay plan. Marca de fin de cadena asíncrona. |

## Streaming (sin `FramePlan`): `stamp` / `clear_now` / `copy_now`

`Screen::stamp(sprite)` abre una **racha** (`StampRun`) que emite copias al Blitter **en el momento**
de cada `at(x,y,frame)`, esperando al blit anterior. `clear_now(box)` borra una banda; `copy_now`
copia un rect. Requiere dibujar en orden (no mezclar con el plan, que se ejecuta en `present`).

## Fuente y contrato

`engine/include/eng/api/screen.hpp`. Modelo de coste: `docs/engine/architecture/ZERO_COST_FRAME_PATH.md`.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
