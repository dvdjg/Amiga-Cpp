# Referencia — `ui/`

La librería GUI del engine (`eng::ui`): widgets sobre un **árbol intrusivo** sin heap ni `virtual`, un **contexto** que hace hit-test/foco/despacho, y un **compositor con backing store**. La UI **no lee hardware**: consume `UiEvent` que fabrica el puente `os::Msg` → `UiEvent`. Ver `docs/engine/architecture/GUI_LIBRARY.md`.

```
   os::Msg ──ui_bridge/msg_adapter──► UiEvent ──UiContext──► widget (handler)
                                        │            │
   painter (chrome sobre Surface) ◄────┘     compositor (backing → pantalla)
                                             double_buffer (compone en back, flip → publish)
```

## Páginas

| Página | Qué documenta |
|---|---|
| [`interaction.md`](interaction.md) | **Núcleo de interacción** (header hoja, sin árbol ni *backing*): conceptos `HitRectLike`/`HitGridLike`, `hit_test` (rects por capa + rejillas por fórmula) y `FocusRing` (anillo por geometría + `Tab` + activación). Lo comparten la GUI y el HUD de juego (`eng::hud` lo incluye: el HUD se construye **sobre** la GUI). |
| [`widgets.md`](widgets.md) | `Widget`/`WidgetType`/`WidgetFlags` (incl. `WfHover`, mantenido por `UiContext` al mover el puntero), `Panel`/`Label`/`Button`/`CheckBox`/`RadioButton`, `UiContext`, `UiEvent`/`UiEventKind`, `ui_bridge`/`msg_adapter`, `layout`, `UiPainter`/`PaintTarget`/`UiTheme`. |
| [`windows.md`](windows.md) | `Window`/`WindowKind`, `Compositor`/`CompWindow`, `WindowBacking`/`DirtyList`, `DoubleBufferScreen`, `EditBox`/`ListView`/`Slider`/`ScrollBar`, `hardware_cursor`/`keymap`/`keys`. |

## Reglas

- **Sin heap y sin `virtual`**: el árbol es intrusivo (padre/hijo/siguiente) y el despacho por tipo vive **fuera** (`switch` exhaustivo en `widgets.hpp`), de modo que un `Widget` no arrastra vtable.
- **La UI no lee hardware**: consume `UiEvent` (ratón/teclado/joystick/tick); la traducción desde `os::Msg` la hace el puente.
- El **compositor no pinta widgets**: la app dibuja el contenido de una ventana en su `backing.surface` (con `UiPainter`) cuando `needs_repaint`; `present()` solo copia los backings a la pantalla sobre las regiones dañadas.

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
