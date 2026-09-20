# `eng::ui` — capa de interfaz reactiva

Capa de **interfaz de usuario** en miniatura: foco, widgets, regiones sucias y pintado por
`FramePlan`. No lee hardware: consume `UiEvent` que fabrica el **puente** desde los mensajes de
[`eng::os`](../os/README.md) (`MsgType::KeyDown`/`KeyUp`/`MouseMove`/`MouseButton`/`Joystick`).

El modelo (contrato `UiEvent`/`UiContext`, relación con el mini-SO y con `Surface`/`FramePlan`)
está en
[`docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`](../../../../docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md) §8.

## Cabeceras previstas

| Cabecera | Contenido |
|---|---|
| `ui_context.hpp` | `UiEvent` (ratón/tecla/tick), `UiContext` (foco, widgets, `dispatch`, `paint`). |
| `ui_bridge.hpp` | Puente `os::Msg` → `UiEvent` (traducción de entrada, sin lógica de UI). |

La UI **no** sondea el ratón ni el teclado: solo reacciona a los eventos que le llegan por el
puente. Esto hace que el mismo `UiContext` funcione con entrada por mensajes o, en host, con
eventos sintéticos de test.
