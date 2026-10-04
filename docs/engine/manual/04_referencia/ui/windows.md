# Referencia — ventanas, compositor y controles

Ventanas, composición con backing store y los controles de alto nivel.

## Ventanas — `ui/window.hpp`

`WindowKind` (`window.hpp:21`) = `Window` (normal, no modal) / `Popup` (se cierra al pulsar fuera o `Esc`) / `Toast` (efímero, TTL en frames, **no capta input**) / `Dialog` (modal). `Window` (`:29`) es un contenedor con marco, `title`, `ttl` y `close_on_outside`. `window_set_kind(w, k)` (`:43`) fija el tipo y los flags derivados (`Dialog` → modal; `Toast` → sin input).

Las ventanas **son widgets**: viven en el árbol (hijos del escritorio/raíz) y su orden en la lista es el orden Z (`raise` las sube al frente). La política (modalidad, cerrar popups, TTL) la aplica `UiContext`. Ver `docs/engine/architecture/GUI_LIBRARY.md` §13.

## Compositor — `ui/compositor.hpp`, `ui/backing.hpp`, `ui/dirty.hpp`

`Compositor` (`compositor.hpp:30`) compone con **backing store**: cada ventana tiene su `WindowBacking` y la pantalla solo **compone** rectángulos (fondo → frente). Mover, redimensionar o cambiar Z **no** invalida a las vecinas: se copian trozos ya rasterizados. `kMaxWindows = 8` (`:31`); `CompWindow` (`:25`) es la ventana del compositor (su backing + su rect en pantalla). El compositor **no pinta widgets**: la app dibuja el contenido en `backing.surface` (con `UiPainter`) cuando `needs_repaint`; `present()` solo hace las copias a la pantalla sobre las regiones dañadas. `DirtyList` (`dirty.hpp`) es la lista de regiones sucias con fusión simple y capacidad fija. Ver `GUI_LIBRARY.md` §14.

## Doble buffer — `ui/double_buffer.hpp`

`DoubleBufferScreen` (`double_buffer.hpp:29`) da dos buffers de pantalla y un **publish** (flip): el compositor compone en el **trasero** (`back()`) mientras el display lee el **delantero** (`front()`); `flip()` intercambia y llama al *publisher*, que publica el nuevo delantero (en Amiga, parcheo de `BPLxPT`/`COP1LC` en VBlank). `bind(mem_a, mem_b, bytes, w, h, depth)` (`:40`) enlaza los dos buffers (memoria del llamador; no posee memoria). El *publisher* es un *seam* (`PublishFn`, `:32`, no propietario): sin él, `flip()` solo intercambia (útil en host y para el test).

```
db.present(compositor, plan);   // compone en back() y hace flip() -> publica
```

## Controles — `ui/editbox.hpp`, `ui/list.hpp`, `ui/slider.hpp`, `ui/scroll.hpp`

| Control | Qué es |
|---|---|
| `EditBox` (`editbox.hpp`) | Campo de texto sobre un **buffer externo** (`buf`/`cap`/`len`). |
| `ListView` (`list.hpp`) | Lista de cadenas con **selección** y desplazamiento vertical. |
| `Slider` (`slider.hpp`) | Control deslizante sobre un `s16*` externo (`min..max`), sin heap. |
| `ScrollBar` (`scroll.hpp`) | Barra de desplazamiento sobre un `s16*` externo (`min..max`). |

## Entrada de bajo nivel — `ui/hardware_cursor.hpp`, `ui/keymap.hpp`, `ui/keys.hpp`

`hardware_cursor.hpp` dibuja el cursor de ratón con un **sprite de hardware** de 16×16. `keys.hpp` define las **teclas lógicas** (el contrato que consume `UiEvent::key`). `keymap.hpp` traduce el **rawkey** Amiga a carácter de UI por **distribución nacional** (`KeyboardLayout`) y gestiona el acento muerto (`DeadKeyState`).

Volver al [índice de `ui/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
