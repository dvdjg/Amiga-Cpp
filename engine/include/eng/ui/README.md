# `eng::ui` — librería GUI para juegos (Amiga)

Librería de **interfaz de usuario** en miniatura montada sobre `field::Surface` + `field::Rasterizer`
(CPU/Blitter) + `graphics::FramePlan`: primitivas de *chrome* (cajas, marcos, líneas, texto),
tema/branding configurable, widgets, eventos, foco, dirty rects y ventanas con **compositor y
backing store** (mover/redimensionar sin invalidar a las vecinas).

El diseño canónico está en
[`docs/engine/architecture/GUI_LIBRARY.md`](../../../../docs/engine/architecture/GUI_LIBRARY.md)
y el plan de fases en
[`docs/guides/roadmap/ROADMAP_GUI.md`](../../../../docs/guides/roadmap/ROADMAP_GUI.md).

No reinventa el dibujo: reutiliza `Surface` (`fill_rect`/`draw_line`/`draw_text`/`blit`/
`blit_masked`), las fuentes `Font8`/`Font5x7` y `field::FlatPlayfield` como backing de ventana.
No depende del mini-SO (`eng::os`), pero se integra con él por el puente `os::Msg` → `UiEvent`.

## Cabeceras previstas

| Cabecera | Contenido |
|---|---|
| `theme.hpp` | `UiTheme` (colores lógicos + métricas) y presets (`kThemeWb13`/`kThemeWb2`/`kThemeFlat`). |
| `painter.hpp` | `UiPainter`: *chrome* sobre `Surface` (fills, marcos, bevels, paneles, glifos). |
| `text.hpp` | `text_width`, `draw_text_clipped` (reusa `Font8`/`Font5x7`). |
| `widget.hpp` | `WidgetType`, `WidgetFlags` y `Widget` (árbol intrusivo, sin heap). |
| `dirty.hpp` | `DirtyList<Max>` con fusión de regiones (rects = `eng::Box`). |
| `event.hpp` | `UiEvent`, `UiEventKind`. **Implementado** (HOST-220). |
| `widgets.hpp` | `Panel`, `Label`, `Button`, `CheckBox`, `RadioButton`. |
| `editbox.hpp` | `EditBox` (buffer externo, caret, foco). |
| `layout.hpp` | `layout_stack_v`/`layout_stack_h` y anclaje. |
| `window.hpp` | `Window`, `WindowKind` (`Window`/`Popup`/`Toast`/`Dialog`) y Z-order. |
| `backing.hpp` | `WindowBacking` (`FlatPlayfield` + `Surface` en Chip RAM). |
| `compositor.hpp` | `Compositor` (move/resize/raise/damage/present; *copies* de backings a pantalla). |
| `context.hpp` | `UiContext` (dirty, foco, hit-test, dispatch, paint). |
| `ui_bridge.hpp` | Puente `eng::os::Msg` → `UiEvent`. **Implementado** (HOST-220). |

Los rectángulos de UI son `eng::Box` (`eng/core/box.hpp`); no hay un `rect.hpp` propio. Reglas del
engine: sin heap en el camino caliente, sin excepciones ni RTTI, `gnu++23`, tipos de `eng/core`,
despacho por tipo (no `virtual` en caliente), capacidad fija y temas/métricas `constexpr`.
