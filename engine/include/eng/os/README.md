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

## Cabeceras

| Cabecera | Contenido | Estado |
|---|---|---|
| `message.hpp` | `MsgType` (contiguo), `Msg` (unión de payloads), `Signal` (máscara de bits). | **Implementado** (HOST-219) |
| `port.hpp` | `MsgQueue<N>` (anillo SPSC IRQ-safe), `PrioMsgQueue<N>`/`MsgPrio`, `MsgPort<N>`, `VBlankLatch`/`take_vblank`. | **Implementado** (HOST-219/236) |
| `dispatch.hpp` | `HandlerTable<Ctx>` (despacho por tabla indexada por `MsgType`) y `dispatch_all`. | **Implementado** (HOST-237) |
| `os.hpp` | Fachada: `system_port`, `frame_count`, `tick`, `post_user`, `request_quit`. | **Implementado** (backend Amiga, demo 208) |
| `file.hpp` | E/S asíncrona: `FileHandle`, `file_open`/`read_async`/`write_async`/`close`/`delete`/`rename`, `IoNotify`/`IoUser`. | **Contrato** (HOST-255); backend `dos`/`trackdisk` previsto |
| `stream.hpp` | `ChunkStream<NumBuffers>`: doble/triple buffer con `request_mask`/`on_chunk_ready`/`advance`/`underrun`/`eof`. | **Implementado** (HOST-257) |
| `time.hpp` | Tiempo: conversiones ticks↔µs (PAL/NTSC) y `TickSource`/`ScopedTimer`. | **Implementado** (HOST-238) |
| `timer.hpp` | `TimerService` (timers de frames/µs → `MsgType::Timer`). | **Implementado** (HOST-222) |
| `input.hpp` | Productores puros de entrada (`JoyProducer`/`PadProducer`/`MouseProducer`): emiten solo al cambiar. | **Implementado** (HOST-252) |
| `message_pump.hpp` | `MessagePumpGame<App>`: drena el puerto en `update` y llama a `on_frame`/`on_render`. | **Implementado** (HOST-253) |
| `task.hpp` | `TaskSystem`: tareas de fondo con ciclo de vida, scheduler de idle, `request_preempt`/`yield_if_preempt`, `TaskMsgPort` propio y tareas-corrutina. | prevista |

Reglas del engine: sin heap, sin excepciones ni RTTI, `gnu++23`, tipos de `eng/core`, API
paramétrica y agnóstica del backend. El backend Amiga (`amiga_minimal`) es quien produce los
mensajes desde sus ISR; el host puede implementar los mismos servicios con `std` para test.
