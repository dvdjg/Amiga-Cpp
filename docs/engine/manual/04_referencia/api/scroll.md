# Referencia — `scroll` (`eng/api/scroll.hpp`)

`eng/api/scroll.hpp` fija el **vocabulario de scroll**: dos capas que el juego usa sin conocer registros,
geometrías ni buffers.

- **Datos de juego** (sin hardware): `ScrollSpec` (qué técnica + período toroidal + velocidad),
  `ScrollKind` (la técnica), `Camera2D` (la ventana al mundo).
- **Motores** (los construye el engine): `playfield::StripScrollLayer` (tiras) y
  `playfield::XlimitedScene` (corcóscru). Se registran con `App::add_scroll_layer` y se alimentan con
  la cámara.

## `ScrollKind` (técnica pedida)

`None` · `Fine` · `BlitterColumns` · `CopperRing` · `CopperSplit` · `Strip`. El planner la acepta,
**degrada** (`CopperSplit → CopperRing → Fine → None`) o rechaza según el coste de Copper por línea
(`eng/scene/scroll_plan.hpp`).

## `ScrollSpec`

| Campo | Significado |
|---|---|
| `kind` | `ScrollKind` pedido. |
| `map_period_words` | período del mapa **toroidal** en words (`0` = mapa acotado). `wraps()` lo consulta. |
| `speed_px` | velocidad máxima (px/frame); acota las **guardas**. |

## `Camera2D` (`scene/virtual_scene.hpp`)

| Método | Firma | Devuelve |
|---|---|---|
| `reset` | `void reset(WorldRect, Size2u viewport)` | — (mundo **acotado**: recorta a sus límites). |
| `reset_ring` | `void reset_ring(u16 period_x, u16 period_y, Size2u viewport)` | — (mundo **toroidal**: posición en `[0,period)` por **envoltura**). |
| `move_by` | `void move_by(s16 dx, s16 dy)` | — (mueve; recorta o envuelve según el tipo). |
| `begin_frame` | `void begin_frame()` | — (guarda el previo → `delta_x/y`). |
| `scroll_x`/`scroll_y`, `x`/`y` | `u16 …() const` | posición de la vista (px de mundo). |
| `set_position`, `center_on`, `delta_x/y`, `moved`, `toroidal` | — | accesores. |

## `TileScroll` (fachada de la capa de tiras)

```cpp
template <class Backend, u16 ViewportW = 320, u16 ViewportH = 256, u8 Planes = 3,
          u16 YTravelPx = 0, u16 MapPeriodWords = 0>
using TileScroll = playfield::StripScrollLayer<…>;
```

Parámetros = **conceptos de juego** (viewport, planos, recorrido Y, período del mapa), no del motor. El
juego la declara como miembro, liga el asset (`set_tilemap`, que **deriva los tamaños**) y la cámara
(`track_camera`/`follow_camera`), y la registra con `App::add_scroll_layer`. Ejemplo completo en
`demos/techniques/amiga/playfield/204_app_strip_scroll`.

> Fuente: `engine/include/eng/api/scroll.hpp`. Arquitectura: `docs/engine/architecture/{PLAYFIELD_SCROLL_ARCHITECTURE,XYLIMITED_ALGORITMO_GENERICO,AMIGA_8WAY_SCROLLING}.md`.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
