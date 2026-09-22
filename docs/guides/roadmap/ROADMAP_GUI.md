# Roadmap de la librería GUI (`eng::ui`)

Plan de implementación del diseño de
[`docs/engine/architecture/GUI_LIBRARY.md`](../../engine/architecture/GUI_LIBRARY.md): primitivas
de *chrome*, tema, widgets, eventos, foco, ventanas y **compositor con backing store por ventana**,
montado sobre `Surface`/`Rasterizer`/`FramePlan` del engine.

## Principios

- **No reinventar el dibujo.** Todo pasa por `field::Surface` y `field::Rasterizer`; la GUI añade
  *chrome*, widgets, tema y composición.
- **Sin heap en el camino caliente.** Árbol intrusivo, listas y pools de capacidad fija.
- **HOST primero.** Painter, tema, dirty, hit-test, foco, edición y compositor son puros y
  host-testables (con una `Surface` de prueba); el hardware se valida con una demo.
- **Evidencia.** Cada fase cierra con su test HOST; la integración se valida con demo
  (build → run → analyze) y el gate visual correspondiente.

## Fases

### G0 — `UiPainter` + `UiTheme` + texto

- **Entregable**: `eng/ui/theme.hpp` (`UiTheme` + presets `kThemeWb13`/`kThemeWb2`/`kThemeFlat`),
  `eng/ui/painter.hpp` (`UiPainter` sobre `Surface`) y `eng/ui/text.hpp` (`text_width`,
  `draw_text_clipped`). Los rectángulos son `eng::Box` (`eng/core/box.hpp`).
- **Reutiliza**: `Surface::{fill_rect,draw_line,draw_text,draw_text_literal}`, `Font8`/`Font5x7`.
- **Verificación**: **HOST-223** — `bevel_out`/`bevel_in` pintan los 4 lados con los colores del
  tema en la posición correcta; `panel` rellena + bevel; `text_width` mide; `Rect` helpers
  (`contains`/`inset`/`intersect`/`merge`).
- **Estado**: **entregado** (`theme.hpp`, `painter.hpp`, `text.hpp`; **HOST-223**). Verificado en
  host pintando sobre `ContiguousPlayfield` y leyendo el color por píxel; falta la demo en
  hardware (G8).

### G1 — Widget base + Panel/Label + dirty

- **Entregable**: `eng/ui/widget.hpp` (`WidgetType`, `WidgetFlags`, `Widget` intrusivo) y
  `eng/ui/dirty.hpp` (`DirtyList<Max>` con fusión); `Panel` y `Label`.
- **Verificación**: **HOST-224** — el árbol enlaza hijos/padres; `mark_dirty` propaga; la lista de
  dirty fusiona solapes y desborda a full-screen; `Panel`/`Label` dibujan en una `Surface` de
  prueba.
- **Estado**: **entregado** (`widget.hpp` + `dirty.hpp` + `widgets.hpp`; **HOST-224**). Verificado
  en host leyendo el color por píxel; falta la demo en hardware (G8).
- **Decisiones fijadas aquí**: despacho con **`switch` exhaustivo** sobre `WidgetType` (no tabla de
  punteros ni `virtual`), y métrica de `measure` (texto `text_width × 8`; contenedor = `bounds`;
  botón `text_width + 2*pad_x × btn_h` en G2).

### G2 — Ratón, hit-test y Button

- **Entregable**: `UiEvent` y `UiContext::dispatch` para ratón; hit-test de delante hacia atrás;
  `Button` (cara + bevel según `WfPressed`, `on_click`).
- **Verificación**: **HOST-225** — el hit-test devuelve el widget más al frente; un `MouseDown`
  fuera no consume; `MouseDown`+`MouseUp` dentro dispara `on_click` una vez; `MouseUp` fuera no.
- **Estado**: pendiente.

### G3 — CheckBox, RadioButton y grupos

- **Entregable**: `CheckBox` (tick 1-bit, alterna `bool*`), `RadioButton` con `group_id` (activa
  uno y desactiva el grupo, dirty de todos).
- **Verificación**: **HOST-226** — toggle de check; un grupo solo tiene un activo; el estado de
  `bool*`/`u8*` externo cambia; los afectados quedan sucios.
- **Estado**: pendiente.

### G4 — Foco de teclado y EditBox

- **Entregable**: foco (`WfFocused`, `Tab`/`Shift+Tab`), `EditBox` (buffer externo, caret,
  inserción/borrado, `view` horizontal), `UiEventKind::Tick` para el caret.
- **Verificación**: **HOST-227** — inserción en medio, backspace, `Left`/`Right`, límite de
  capacidad, `ensure_caret_visible`; `Tab` cicla solo dentro del modal.
- **Estado**: pendiente.

### G5 — Layout y presets de tema

- **Entregable**: `layout_stack_v`/`layout_stack_h` + anclaje; presets de tema de *branding*;
  cambio de tema en caliente (dirty de todo).
- **Verificación**: **HOST-228** — la pila coloca con `gap`/paddings; un botón mide por
  `text_width` + `btn_h`; cambiar de tema recolorea los widgets.
- **Estado**: pendiente.

### G6 — Ventanas: Window/Popup/Toast/Dialog

- **Entregable**: `Window` con Z-order, `Popup` (`close_on_outside`, `Esc`), `Toast` (TTL por
  frames, no capta input) y `Dialog` (modal: filtra hit-test y foco).
- **Verificación**: **HOST-229** — `raise` cambia el orden; el modal bloquea el escritorio; `Esc`
  cierra el diálogo/popup superior; el toast expira tras su TTL.
- **Estado**: pendiente.

### G7 — Compositor con backing store

- **Entregable**: `eng/ui/backing.hpp` (`WindowBacking` = `FlatPlayfield` + `Surface` sobre Chip)
  y `eng/ui/compositor.hpp` (`move_window`/`resize_window`/`raise`/`damage_screen`/`present`).
- **Modelo**: cada ventana pinta en su backing; la pantalla solo compone *copies* en orden Z. Mover
  /redimensionar/cambiar Z **no** invalida a las vecinas.
- **Verificación**: **HOST-230** — `move_window` daña origen y destino sin marcar `content_dirty`
  ajeno; `compose_region` copia los trozos correctos de los backings (comparar contra un patrón
  de referencia); `resize` marca solo su ventana; el pool falla limpio si no cabe.
- **Estado**: pendiente.

### G8 — Aceleración Blitter y demo en hardware

- **Entregable**: *fills* y marcos por `FramePlan` en el mismo plan del frame; **`fill_rect`
  D-only** en el `BlitterRaster` para cajas axis-aligned (minterm `$FF`, sin fuentes); cursor por
  **sprite de hardware**.
- **Verificación**: **demo 207_gui_widgets** (G0–G6) y **demo 208_gui_compositor** (G7) en
  hardware, con gate visual (widgets presentes, ventana que se mueve/redimensiona y se recomponen
  los vecinos). Medir con el profiler.
- **Estado**: pendiente.

## Tests y demos previstos

| ID | Tipo | Contenido |
|---|---|---|
| HOST-223 | test | `UiPainter`/`UiTheme`/`Rect`/medida de texto. |
| HOST-224 | test | `Widget` base, `Panel`/`Label`, `DirtyList`. |
| HOST-225 | test | Ratón, hit-test y `Button`. |
| HOST-226 | test | `CheckBox`, `RadioButton` y grupos. |
| HOST-227 | test | Foco de teclado y `EditBox`. |
| HOST-228 | test | Layout y cambio de tema. |
| HOST-229 | test | Ventanas, popup, toast y modal. |
| HOST-230 | test | Compositor y backing store (move/resize/raise/compose). |
| 207_gui_widgets | demo | Widgets y tema en hardware (G0–G6). |
| 208_gui_compositor | demo | Ventanas movibles/redimensionables con backing store (G7). |

## Riesgos y decisiones abiertas

- **Memoria de backings (A500).** El límite principal es la Chip RAM: hay que fijar el pool
  (nº y tamaño máximo de ventanas) y decidir el comportamiento al no caber (fallo limpio vs
  *clamp*).
- **Rectángulo de UI.** Resuelto: la GUI usa `eng::Box` (rect único del engine, HOST-231) con
  adaptadores a `SurfaceRect`/`ClipRect`/`DirtyRect`; no define un rect propio.
- **Despacho de widgets.** **Decidido en G1**: `switch` exhaustivo sobre `WidgetType` (no tabla de
  punteros ni `virtual`); se mantiene estable.
- **`fill_rect` D-only.** Añadir el camino de *fill* por Blitter sin fuentes al raster mejora los
  *clears* de UI; conviene hacerlo en G8 con test de equivalencia contra el camino de polígono.
- **Rubber-band vs live resize.** Para A500, *rubber-band*; el *live* queda como opción si el
  presupuesto lo permite.
- **Iconos/glifos.** Los glifos 1-bit (tick, flechas, radio) se definen como tablas `constexpr`
  incrustadas; no se generan desde ficheros.

## Estado

**G0–G1 entregados** (`theme.hpp`/`painter.hpp`/`text.hpp` HOST-223; `widget.hpp`/`dirty.hpp`/
`widgets.hpp` con `Panel`/`Label` HOST-224). G2–G8 pendientes. El siguiente paso es **G2**
(ratón + hit-test + `Button`), que sigue sin depender del compositor ni del mini-SO.

La GUI queda **fuera** de `eng/api/api.hpp` hasta que exista su primer consumidor hardware (demo
G8); entonces se expondrá solo la superficie estable. El `fill_rect` D-only del raster se deja
para G8, con test de equivalencia.
