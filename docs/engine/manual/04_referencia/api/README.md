# Referencia — `api/` (la fachada)

El módulo `eng::api` (`engine/include/eng/api/`) es **lo único que un juego necesita incluir**. Su
cabecera de entrada es `api.hpp` (reexporta todo sin definir tipos nuevos).

| Cabecera | Contenido |
|---|---|
| `api.hpp` | Include único: `game`, `display`, `scene`, `screen`, `scroll`, `sprites`, `framebuffer`, `assets`, `copper`, `device`, `effects`, `world_render` + tipos base. |
| [`game.hpp`](game.md) | **`App`** (composition root), `Screen`, `Device`, `GameDisplay`, escenas, memoria, entrada, assets. |
| [`screen.hpp`](screen.md) | `Screen`: contexto de dibujo de alto nivel (`clear`/`fill`/`frame`/`line`/`text`/`sprite`). |
| [`scene.hpp`](scene.md) | Vocabulario de escena (`ScenePlan`/`LayerPlan`/… + estrategias `apply_dpf_plan`/`plan_bands`/`plan_raster_layout`). |
| [`scroll.hpp`](scroll.md) | `ScrollSpec`, `ScrollKind`, `Camera2D`, `TileScroll` (motores de scroll). |
| [`sprites.hpp`](sprites.md) | `SpriteScene<MaxActors>`: fachada de sprites (HW + BOB). |
| [`framebuffer.hpp`](framebuffer.md) | `IndexedDisplay<Planes,Buffers>`: framebuffer indexado + C2P. |
| [`display.hpp`](display.md) | `GameDisplay`: descripción del display. |
| [`assets.hpp`](assets.md) | `Assets`: registro/consulta de assets cocinados. |
| [`copper.hpp`](copper.md), [`device.hpp`](device.md) | Servicios de hardware agrupados (`Device`: Blitter/Copper/raster). |
| [`effects.hpp`](effects.md) | Efectos de alto nivel (`CopperChunky`, `Gradient`, `Rotozoom`, `SpriteLayer`, `FineScroll`). |
| [`world_render.hpp`](world_render.md) | Materialización del `World` (capas/actores). |

> Cada página de referencia documenta **propósito, firma, parámetros, retorno y errores** de cada
> método, con `fichero:línea` al fuente (`engine/include/eng/api/*.hpp`).

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
