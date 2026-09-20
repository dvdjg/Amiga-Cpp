# `eng::os` — mini-SO de mensajes (capa de abstracción)

Capa de **servicios de sistema en miniatura** sobre el engine: puerto de mensajes, señales y
productores de eventos (VBlank, entrada, E/S de disco, timers). Sustituye la espera activa por
un **bucle reactivo**: la aplicación pide `wait(signals)` y consume mensajes, igual que un
*message pump* de un SO de escritorio, pero sin SO: en la máquina tomada el motor es la IRQ.

El diseño canónico (modelo, contrato de cada pieza y decisiones) está en
[`docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`](../../../../docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md)
y el plan de fases en
[`docs/guides/roadmap/ROADMAP_MINI_OS.md`](../../../../docs/guides/roadmap/ROADMAP_MINI_OS.md).

## Cabeceras previstas

| Cabecera | Contenido |
|---|---|
| `message.hpp` | `MsgType`, `Msg` (unión de payloads), `Signal` (máscara de bits). |
| `port.hpp` | `MsgQueue<N>` (anillo SPSC IRQ-safe) y `MsgPort` (cola + `signalled` + `wait`/`signal`). |
| `os.hpp` | Fachada de servicios: `init`, `system_port`, `frame_count`, `add_timer`, `input_enable`, `post_user`, `request_quit`. |
| `file.hpp` | E/S asíncrona: `FileHandle`, `file_open`/`read_async`/`write_async`/`close`, y `MsgType::FileDone`/`FileError`. |

Reglas del engine: sin heap, sin excepciones ni RTTI, `gnu++23`, tipos de `eng/core`, API
paramétrica y agnóstica del backend. El backend Amiga (`amiga_minimal`) es quien produce los
mensajes desde sus ISR; el host puede implementar los mismos servicios con `std` para test.
