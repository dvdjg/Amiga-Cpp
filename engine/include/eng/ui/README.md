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

## Cabeceras

`ui.hpp` es la **fachada** (un solo include con la superficie estable); la incluye
`eng/api/api.hpp`.

| Cabecera | Contenido |
|---|---|
| `theme.hpp` | `UiTheme` (colores lógicos + métricas) y presets (`kThemeWb13`/`kThemeWb2`/`kThemeFlat`); `Rect` = `eng::Box`. **Implementado** (HOST-223). |
| `painter.hpp` | `UiPainter`: *chrome* sobre `Surface` (fills, marcos, bevels, paneles, glifos). **Implementado** (HOST-223). |
| `text.hpp` | `text_width`, `draw_text_clipped` (reusa `Font8`/`Font5x7`). **Implementado** (HOST-223). |
| `widget.hpp` | `WidgetType`, `WidgetFlags` y `Widget` (árbol intrusivo, sin heap ni `virtual`). **Implementado** (HOST-224). |
| `dirty.hpp` | `DirtyList<Max>` con fusión de regiones (rects = `eng::Box`). **Implementado** (HOST-224). |
| `event.hpp` | `UiEvent`, `UiEventKind`. **Implementado** (HOST-220). |
| `widgets.hpp` | `Panel`/`Label` (G1), `Button` (G2), `CheckBox`/`RadioButton` (G3), `Window` (G6), `Slider`. Despacho por `switch` exhaustivo y `measure`. **Implementado** (HOST-224/225/226/229/262). |
| `keys.hpp` | Teclas lógicas de la UI (imprimibles ASCII/Latin-1 + edición/navegación). **Implementado** (HOST-227). |
| `keymap.hpp` | Traducción **rawkey Amiga → carácter** por **distribución nacional** (US/ES/FR/IT/DE/RU) + Shift + **teclas muertas** (`compose`/`rawkey_to_char`). **Implementado** (HOST-261/263/265); validar las tablas contra el ROM, pendiente. |
| `editbox.hpp` | `EditBox` (buffer externo, caret, vista horizontal). **Implementado** (HOST-227). |
| `slider.hpp` | `Slider` (sobre `s16*`, click/arrastre y flechas). **Implementado** (HOST-262). |
| `context.hpp` | `UiContext`: hit-test, foco (`Tab`), modalidad, `Esc`, popups, TTL de toasts y la **distribución nacional** (`layout`, la fija la app al arrancar). **Implementado** (HOST-225/229). |
| `layout.hpp` | `layout_stack_v`/`layout_stack_h` y `anchor`. **Implementado** (HOST-228). |
| `window.hpp` | `Window`, `WindowKind` (`Window`/`Popup`/`Toast`/`Dialog`) y Z-order. **Implementado** (HOST-229). |
| `backing.hpp` | `WindowBacking` (lienzo planar + `Surface`). **Implementado** (HOST-230). |
| `compositor.hpp` | `Compositor` (add/raise/move/resize/damage/`present`; `present_blit` copia por `Surface::blit` con vuelta a CPU). **Implementado** (HOST-230/267). |
| `ui_bridge.hpp` | Puente `eng::os::Msg` → `UiEvent`. **Implementado** (HOST-220). |
| `msg_adapter.hpp` | `dispatch_msg`: `os::Msg` de entrada → `UiContext` (traduce con `ctx.layout` y compone con `ctx.dead`). **Implementado** (HOST-261/263/265). |

Los rectángulos de UI son `eng::Box` (`eng/core/box.hpp`); no hay un `rect.hpp` propio. Reglas del
engine: sin heap en el camino caliente, sin excepciones ni RTTI, `gnu++23`, tipos de `eng/core`,
despacho por tipo (no `virtual` en caliente), capacidad fija y temas/métricas `constexpr`.
