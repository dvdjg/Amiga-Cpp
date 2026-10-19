# `eng::hud` — HUD de juego (Amiga)

Modelo de **HUD de juego**: medidores, contadores, rejillas de iconos, slots, arrastre, etiquetas, botones y paneles, con *handles* tipados, capacidades fijas y *dirty* explícito. Es **puro** (sin hardware, sin `eng::ui`, sin `eng::rpg`): el juego es la única fuente de verdad y el HUD refleja valores o **vistas** (`HudSlotView`).

El diseño canónico está en [`docs/engine/architecture/HUD_SYSTEM.md`](../../../../docs/engine/architecture/HUD_SYSTEM.md) y el plan de fases en [`docs/guides/roadmap/ROADMAP_HUD.md`](../../../../docs/guides/roadmap/ROADMAP_HUD.md).

La **presentación** (franja `CanvasPlayfield`, overlay con *backing*, sprites, Blitter y `copper::Plan`) llega en las fases H1–H4; la franja (`strip.hpp`) está entregada y verificada con la demo `302_hud_band`. Reglas del engine: sin heap en el camino caliente, sin excepciones ni RTTI, `gnu++23`, capacidades como parámetros de plantilla y despacho por tipo (no `virtual`).

## Cabeceras

`hud.hpp` es la **fachada** (un solo include con la superficie estable).

| Cabecera | Contenido |
|---|---|
| `model.hpp` | `Hud<...>` con *handles* tipados, `Meter`, `Counter` (formato sin división), `IconGrid`, `HudSlotView`, `Label`, `Button`, `Panel`, `DragSession`, listas de `HudDrawOp`/`HudHitRect`/`HudGridHit` y revisión de *dirty*. **Implementado** (HOST-467; demo 302). |
| `region.hpp` | `HudPlacement` (`Band`/`Overlay`/`Screen`), `HudBand`, `HudBudget`, `HudRegionSpec` y validadores puros `band_valid`/`region_valid`. **Implementado** (HOST-467; demo 302). |
| `budget.hpp` | `hud_backing_bytes`/`hud_backing_fits` (coste declarado), `hud_channels_conflict` y `HudDegradeLadder` (escalera determinista con histéresis, `HUD_SYSTEM.md` §6.7). **Implementado** (HOST-471; demos 305/306). |
| `strip.hpp` | `HudStrip` (lienzo `CanvasPlayfield` + `Surface` de la banda) y `apply_hud_ops` (materialización CPU de `Fill`/`Frame`/`Text`/`Digits` con recorte; texto por palabras en celdas alineadas). **Implementado** (HOST-468; demo 302). |
| `theme.hpp` | `MeterStyle`, `CounterStyle`, `HudFont`, `IconAtlas` (memoria Chip tipada), `IconSet` (mapeo tipo→frame + recoloreado, lado juego) y `HudTheme` (roles de color y métricas). **Implementado** (HOST-467; demos 302/306). |

Estado por fase y demos: [`ROADMAP_HUD.md`](../../../../docs/guides/roadmap/ROADMAP_HUD.md). El atlas de iconos y los conjuntos (`IconAtlas`/`IconSet`) se consumen en la demo `306_hud_crpg` (pasada de iconos por CPU sobre la banda).
