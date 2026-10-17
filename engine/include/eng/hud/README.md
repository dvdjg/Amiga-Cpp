# `eng::hud` — HUD de juego (Amiga)

Modelo de **HUD de juego**: medidores, contadores, rejillas de iconos, slots, arrastre, etiquetas, botones y paneles, con *handles* tipados, capacidades fijas y *dirty* explícito. Es **puro** (sin hardware, sin `eng::ui`, sin `eng::rpg`): el juego es la única fuente de verdad y el HUD refleja valores o **vistas** (`HudSlotView`).

El diseño canónico está en [`docs/engine/architecture/HUD_SYSTEM.md`](../../../../docs/engine/architecture/HUD_SYSTEM.md) y el plan de fases en [`docs/guides/roadmap/ROADMAP_HUD.md`](../../../../docs/guides/roadmap/ROADMAP_HUD.md).

La **presentación** (franja `CanvasPlayfield`, overlay con *backing*, sprites, Blitter y `copper::Plan`) llega en las fases H1–H4; este módulo solo fija el contrato, el modelo y los validadores. Reglas del engine: sin heap en el camino caliente, sin excepciones ni RTTI, `gnu++23`, capacidades como parámetros de plantilla y despacho por tipo (no `virtual`).

## Cabeceras

`hud.hpp` es la **fachada** (un solo include con la superficie estable).

| Cabecera | Contenido |
|---|---|
| `model.hpp` | `Hud<...>` con *handles* tipados, `Meter`, `Counter` (formato sin división), `IconGrid`, `HudSlotView`, `Label`, `Button`, `Panel`, `DragSession`, listas de `HudDrawOp`/`HudHitRect`/`HudGridHit` y revisión de *dirty*. **Implementado** (HOST-467). |
| `region.hpp` | `HudPlacement` (`Band`/`Overlay`/`Screen`), `HudBand`, `HudBudget`, `HudRegionSpec` y validadores puros `band_valid`/`region_valid`. **Implementado** (HOST-467). |
| `strip.hpp` | `HudStrip` (lienzo `CanvasPlayfield` + `Surface` de la banda) y `apply_hud_ops` (materialización CPU de `Fill`/`Frame`/`Text`/`Digits` con recorte). **Implementado** (HOST-468). |
| `theme.hpp` | `MeterStyle`, `HudFont`, `IconAtlas` (memoria Chip tipada) y `HudTheme` (roles de color y métricas). **Implementado** (HOST-467). |

**NO VERIFICADA por demo**: la primera demo del HUD (`302_hud_band`) es la fase H1 del roadmap; hasta entonces la API puede cambiar sin aviso (`docs/testing/README.md` §Verificación por demo).
