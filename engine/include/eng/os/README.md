# `eng::os` — mini-SO de mensajes (capa de abstracción)

Capa de **servicios de sistema en miniatura** sobre el engine: puerto de mensajes, señales y
productores de eventos (VBlank, entrada, E/S de disco, timers). Sustituye la espera activa por
un **bucle reactivo**: la aplicación pide `wait(signals)` y consume mensajes, igual que un
*message pump* de un SO de escritorio, pero sin SO: en la máquina tomada el motor es la IRQ.

El diseño canónico está en la familia de documentos del mini-SO
([`MINI_OS_MESSAGE_LOOP.md`](../../../../docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md) para el
núcleo, [`MINI_OS_INPUT.md`](../../../../docs/engine/architecture/MINI_OS_INPUT.md),
[`MINI_OS_TIME.md`](../../../../docs/engine/architecture/MINI_OS_TIME.md) y
[`MINI_OS_IO.md`](../../../../docs/engine/architecture/MINI_OS_IO.md)), y el plan de fases en
[`docs/guides/roadmap/ROADMAP_MINI_OS.md`](../../../../docs/guides/roadmap/ROADMAP_MINI_OS.md).

## Cabeceras previstas

| Cabecera | Contenido |
|---|---|
| `message.hpp` | `MsgType` (contiguo), `Msg` (unión de payloads), `Signal` (máscara de bits). |
| `port.hpp` | `MsgQueue<N>` (anillo SPSC IRQ-safe) y `MsgPort` (cola + `signalled` + `wait`/`signal`), `MsgPrio`/`PrioMsgQueue` y VBlank latched. |
| `os.hpp` | Fachada de servicios: `init`, `system_port`, `frame_count`, `add_timer`, `input_enable`, `post_user`, `request_quit`. |
| `time.hpp` | Tiempo: `TickClock` (µs sobre CIA-B), `ScopedTimer`, `beam_now`. |
| `timer.hpp` | `TimerService` (timers de frames/µs → `MsgType::Timer`) y one-shot de CIA. |
| `file.hpp` | E/S asíncrona: `FileHandle`, `file_open`/`read_async`/`write_async`/`close`, `IoNotify`, `AudioStream` y `MsgType::FileDone`/`FileError`. |

Reglas del engine: sin heap, sin excepciones ni RTTI, `gnu++23`, tipos de `eng/core`, API
paramétrica y agnóstica del backend. El backend Amiga (`amiga_minimal`) es quien produce los
mensajes desde sus ISR; el host puede implementar los mismos servicios con `std` para test.
