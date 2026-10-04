# Referencia — widgets, contexto y pintado

El **árbol de widgets** de `eng::ui`: datos planos + árbol intrusivo (padre/hijo/siguiente), **sin heap y sin `virtual`**. El despacho por tipo vive fuera (un `switch` exhaustivo en `widgets.hpp`), de modo que un `Widget` no arrastra vtable.

## `Widget` — `ui/widget.hpp`

`WidgetType` (`widget.hpp:16`) es el discriminante (`Panel`/`Label`/`Button`/`Check`/`Radio`/`Edit`/`Slider`/`ScrollBar`/`List`/`Window`). `WidgetFlags` (`:30`) es la máscara (`WfVisible`/`WfEnabled`/`WfDirty`/`WfFocused`/`WfPressed`/`WfModal`/`WfAcceptsFocus`/`WfNoInput`). `Widget` (`:44`) es el nodo: `type`/`bounds`/`flags` + `parent`/`first_child`/`next`. **Los hijos se insertan al frente**: el orden de la lista de hermanos es el orden Z (el primero = el más al frente). `children_creation_order<Max>` (`:109`) recorre los hijos en orden de creación.

## Widgets concretos — `ui/widgets.hpp`

`Panel` (`widgets.hpp:32`), `Label` (`:38`), `Button` (`:48`), `CheckBox` (`:61`), `RadioButton` (`:73`) derivan de `Widget` con sus datos propios (no una unión opaca). `kTickGlyph` (`:86`) es el glifo de marca; `measure(w, theme)` (`:91`) da el tamaño mínimo de un widget.

## Contexto — `ui/context.hpp`

`UiContext` (`context.hpp:26`) hace **hit-test, foco y despacho** sobre el árbol. No lee hardware: consume `UiEvent`. Reglas de entrada:

- hit-test de **delante hacia atrás**; con un **modal** (`WfModal`) abierto, solo su subárbol.
- el ratón **engancha** el widget en `MouseDown` y le entrega el `MouseUp`.
- `Tab`/`Shift+Tab` cicla el foco (dentro del modal si lo hay).
- `Esc` cierra el popup/diálogo superior; pulsar fuera cierra el popup.
- los `Toast` (`WfNoInput`) no captan input y expiran por `Tick`.

`set_root(r)` fija la raíz; `top_modal()` (`:38`) da el frente de la pila de modales; `hit_test(node, px, py)` (`:42`) devuelve el widget más al frente que contiene el punto (salta los `Toast`). `layout` es la distribución nacional para traducir el rawkey (`keymap.hpp`); `dead` el acento muerto pendiente.

## Eventos y puente

`UiEventKind` (`event.hpp:13`) = `None`/`MouseMove`/`MouseDown`/`MouseUp`/`KeyDown`/`KeyUp`/`JoyButton`/`Tick`. `UiEvent` (`:25`) lleva posición, botones, scancode, modificadores y estado del pad. `to_ui_event(m, out)` (`ui_bridge.hpp:15`) traduce un `os::Msg` a `UiEvent`; `msg_adapter.hpp` lo integra en el bucle. Ver `MINI_OS_MESSAGE_LOOP.md` §8.

## Layout — `ui/layout.hpp`

Sin motor de *constraints*: para A500 basta pila **vertical**/**horizontal** (con `gap`) y **anclaje** a un borde del padre. `kMaxLayoutChildren = 32` (`layout.hpp:16`); `layout_stack_v(g, gap)`/`layout_stack_h(g, gap)` colocan los hijos en orden de creación desde `g.bounds`; `Anchor` (`:48`) enumera los anclajes. Los hijos deben llevar ya su tamaño (p. ej. de `measure(w, theme)`).

## Pintado — `ui/painter.hpp`, `ui/paint_target.hpp`, `ui/theme.hpp`

`UiPainter` (`painter.hpp:26`) pinta el **chrome** de UI sobre una `playfield::Surface`: no dibuja píxeles, delega en `Surface` (que enruta por `Rasterizer` y recorta contra el clip) y aporta las operaciones con concepto de UI (marcos, biseles, paneles, texto con fondo, glifos). `PaintTarget` (`paint_target.hpp:21`) es el destino neutral (`Surface` hoy; `RastPort` con `-DENG_UI_INTUITION`). `UiTheme` (`theme.hpp:29`) son los colores lógicos (índices de la paleta del playfield) y métricas; `FrameStyle` (`:20`); presets `kThemeWb13`/`kThemeWb2`/`kThemeFlat` (`:54`). `text.hpp` mide y recorta texto con `Font8` (no añade fuente nueva).

Volver al [índice de `ui/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
