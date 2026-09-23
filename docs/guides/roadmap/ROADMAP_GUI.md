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
  `draw_text_clipped`). Los rectángulos son `eng::Box` (`eng/core/types/box.hpp`).
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
- **Estado**: **entregado** (`context.hpp` + `Button`; **HOST-225**). Verificado en host; falta la
  demo en hardware (G8).

### G3 — CheckBox, RadioButton y grupos

- **Entregable**: `CheckBox` (tick 1-bit, alterna `bool*`), `RadioButton` con `group_id` (activa
  uno y desactiva el grupo, dirty de todos).
- **Verificación**: **HOST-226** — toggle de check; un grupo solo tiene un activo; el estado de
  `bool*`/`u8*` externo cambia; los afectados quedan sucios.
- **Estado**: **entregado** (`CheckBox`/`RadioButton`; **HOST-226**). El grupo es por `parent`
  (hermanos con el mismo `group_id`); verificado en host.

### G4 — Foco de teclado y EditBox

- **Entregable**: foco (`WfFocused`, `Tab`/`Shift+Tab`), `EditBox` (buffer externo, caret,
  inserción/borrado, `view` horizontal), `UiEventKind::Tick` para el caret.
- **Verificación**: **HOST-227** — inserción en medio, backspace, `Left`/`Right`, límite de
  capacidad, `ensure_caret_visible`; `Tab` cicla dentro del modal (G4 sin modales aún: en todo el
  árbol).
- **Estado**: **entregado** (`keys.hpp` + `editbox.hpp` + foco en `context.hpp`; **HOST-227**). La
  traducción rawkey Amiga → tecla lógica (`keys.hpp`) es del input y está **pendiente**.

### G5 — Layout y presets de tema

- **Entregable**: `layout_stack_v`/`layout_stack_h` + anclaje; presets de tema de *branding*;
  cambio de tema en caliente (dirty de todo).
- **Verificación**: **HOST-228** — la pila coloca con `gap`/paddings; un botón mide por
  `text_width` + `btn_h`; cambiar de tema recolorea los widgets.
- **Estado**: **entregado** (`layout.hpp` + `mark_all_dirty`; **HOST-228**). Los hijos se colocan
  en **orden de creación**.

### G6 — Ventanas: Window/Popup/Toast/Dialog

- **Entregable**: `Window` con Z-order, `Popup` (`close_on_outside`, `Esc`), `Toast` (TTL por
  frames, no capta input) y `Dialog` (modal: filtra hit-test y foco).
- **Verificación**: **HOST-229** — `raise` cambia el orden; el modal bloquea el escritorio; `Esc`
  cierra el diálogo/popup superior; el toast expira tras su TTL.
- **Estado**: **entregado** (`window.hpp` + modalidad/popups/toasts en `context.hpp`; **HOST-229**).

### G7 — Compositor con backing store

- **Entregable**: `eng/ui/backing.hpp` (`WindowBacking` = `FlatPlayfield` + `Surface` sobre Chip)
  y `eng/ui/compositor.hpp` (`move_window`/`resize_window`/`raise`/`damage_screen`/`present`).
- **Modelo**: cada ventana pinta en su backing; la pantalla solo compone *copies* en orden Z. Mover
  /redimensionar/cambiar Z **no** invalida a las vecinas.
- **Verificación**: **HOST-230** — `move_window` daña origen y destino sin marcar `content_dirty`
  ajeno; `compose_region` copia los trozos correctos de los backings (comparar contra un patrón
  de referencia); `resize` marca solo su ventana; el pool falla limpio si no cabe.
- **Estado**: **entregado** (`backing.hpp` + `compositor.hpp`; **HOST-230**). El *copy* es por CPU
  (píxel a píxel); en G8 se acelerará con Blitter (`CopyRect`).

### G8 — Aceleración Blitter y demo en hardware

- **Entregable**: *fills* y marcos por `FramePlan` en el mismo plan del frame; **`fill_rect`
  D-only** en el `BlitterRaster` para cajas axis-aligned (minterm `$FF`, sin fuentes); cursor por
  **sprite de hardware**.
- **Verificación**: **demo 207_gui_widgets** (G0–G6) y **demo 208_gui_compositor** (G7) en
  hardware, con gate visual (widgets presentes, ventana que se mueve/redimensiona y se recomponen
  los vecinos). Medir con el profiler.
- **Demo en hardware**: **`215_gui_widgets`** (escena planar EHB 320×256) ya **renderiza y
  verifica**. La causa del fallo anterior era dibujar con el **`BlitterRaster` instalado**
  (`backend.install_raster`): un *fill* grande por Blitter es **asíncrono** y pisaba los trazos
  CPU del mismo frame. La solución es **no instalar el rasterizador Blitter** en la demo (dibujo
  CPU, síncrono) y **repintar por zona** (la UI es estática salvo la pista del slider). Gate
  objetivo: `tools/analyze/verify-gui-widgets.mjs` (panel/texto/bisel/foco presentes y el slider
  cambia en una banda horizontal entre frames).
- **`fill_rect` D-only por Blitter**: **entregado**. `AmigaBackend::blitter_fill_rect` rellena
  el rect con minterm `$FF`/`$00` (D-only, sin fuente: el Blitter solo ve Chip RAM) y repara por
  CPU la primera/última palabra para x/w no alineados. Se conecta por el *seam*
  `field::RectFillSink` (`Playfield::fill_rect_hw`, `Scene::set_rect_fill_sink`), que
  `BlitterRaster::fill_rect` prefiere al camino de polígono. Contrato en HOST-266; la ruta de
  hardware se valida con el **self-test de la demo 215** (rect relleno y comprobado por bits).
- **Cursor por sprite de hardware**: **entregado**. La demo 215 reserva un canal de sprite (0) con
  una estructura DMA en Chip RAM (POS/CTL/DAT/DATB + terminador), apunta `SPR0PT` a ella y habilita
  `DMACON` SPREN como etapa de `compose`; por frame sigue al ratón (`poll_mouse`) reescribiendo
  POS/CTL. Self-test de la emisión (SPR0PT + SPREN) y de la estructura. **Nota**: la captura PNG del
  runner **no incluye sprites** (la demo 206 tampoco los muestra), así que el cursor se valida por
  registros/copperlist, no por el gate de píxeles.
- **Compositor por Blitter**: **entregado**. `Compositor::present_blit(plan)` copia cada backing con
  `Surface::blit` (ruta del `Rasterizer`: encola `CopyRect` en el `FramePlan`); si el destino no está
  alineado a palabra, cae al copiado por píxel de ese rect. Equivalencia con `present()` en
  HOST-268. La ejecución del plan la hace el llamador (`backend.execute_frame_plan`). **Demo
  `300_gui_compositor`**: tres ventanas con backing que se mueven (pasos de 16 px, copias alineadas)
  y se recomponen por el Blitter; gate visual `verify-gui-compositor.mjs`.
- **Estado**: **G8 completo**: demo en hardware verificada, *fills* de caja por Blitter D-only,
  cursor por sprite de hardware y compositor por `Surface::blit`. Queda, fuera de G8, exponer un
  **cursor de hardware reutilizable** (`eng::ui::HardwareCursor`) y validar los keymaps contra el ROM.

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
| HOST-261 | test | Entrada por **mensajes** (`os::Msg` → `UiContext`) con `keymap` rawkey→tecla lógica. |
| HOST-262 | test | `Slider` (click/arrastre y flechas). |
| HOST-263 | test | Keymaps nacionales (ES/FR/IT/DE/RU) y `dispatch_msg` con el layout del contexto. |
| `215_gui_widgets` | demo | Widgets y tema en hardware (G0–G6). **Entregada y verificada** (G8). |
| `300_gui_compositor` | demo | Ventanas movibles con backing store y **copias por Blitter** (`present_blit`). **Entregada y verificada**. |

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

**G0–G8 entregados**. `theme`/`painter`/`text`, `widget`/`dirty`/`widgets`
(`Panel`/`Label`/`Button`/`CheckBox`/`RadioButton`/`Slider`), `keys`/`keymap`/`editbox`/`context`
(foco), `layout`, `window` (Window/Popup/Toast/Dialog), `backing`/`compositor` y la **entrada por
mensajes** (`msg_adapter` + `ui_bridge`, HOST-261/262) con **keymaps nacionales** (HOST-263) y
**teclas muertas** (HOST-265). La distribución nacional es **estado del `UiContext`** (`ctx.layout`),
que fija la aplicación al arrancar; `dispatch_msg` la usa, sin ir fija en la llamada.

**G8 entregado**: demo `215_gui_widgets` en hardware (gate `verify-gui-widgets.mjs`) y demo
`300_gui_compositor` (ventanas movibles con backing). La superficie estable se expone en
`eng/api/api.hpp` vía `eng/ui/ui.hpp`.

**Aceleración Blitter**: el `fill_rect` D-only (minterm `$FF`) entra por el `RectFillSink` (HOST-266)
y el compositor por `Surface::blit` (`Compositor::present_blit(FramePlan&)`, equivalencia CPU/Blitter
en HOST-268); las copias solo van al Blitter con destino/origen **alineados a palabra** (16 px),
si no caen al bucle de píxeles.

**Extras tras G8**: **`eng::ui::HardwareCursor`** por sprite de hardware (HOST-301), **teclas comunes
del keymap** validadas contra la AHRM 3.ª (HOST-302: Space 0x40, cursores 0x4C/0x4D corregidos),
**cirílico** en `Font8` (HOST-264) y **`EditBox` UTF-8** (HOST-303) para teclearlo en campos.

Pendiente: volcar la asignación de carácter de cada **distribución nacional** y los **Alt+tecla**
de las teclas muertas desde `DEVS:Keymaps` del ROM (no disponibles en el repo; hoy *best-effort*).

## Portabilidad a Workbench (backend de UI)

El diseño de widgets/eventos ya es **neutral** (widgets sin `virtual`, entrada solo por `UiEvent`),
así que la misma app puede correr además en **Workbench/Intuition** (ventanas del OS) sin reescribir
los widgets. El plan está en [`ROADMAP_WORKBENCH.md`](ROADMAP_WORKBENCH.md): añade un *seam* de
dibujo (**`PaintTarget`**: `Surface` | `RastPort`), un contrato de host (**`UiHost`** por plantilla,
sin `virtual`) y un puente **IDCMP → `os::Msg` → `UiEvent`**.
