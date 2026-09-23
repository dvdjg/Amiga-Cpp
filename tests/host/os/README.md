# Tests HOST — os

Categoría `os` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-004 | [input](004_input/README.md) | `eng::input::InputAggregator`: estado portable de entrada (pad CD32, ratón, teclado) — paso 6 de `ENGINE_DESIGN.md` §5. |
| HOST-017 | [background_task](017_background_task/README.md) | `eng::task::BackgroundQueue`: tareas de fondo cooperativas (progreso/rendimiento, adaptación por `vpos`, prioridad al bucle principal). |
| HOST-219 | [os_core](219_os_core/README.md) | Mini-SO núcleo (`eng/os/message.hpp` + `port.hpp`): `Msg` trivial, `MsgQueue` SPSC (FIFO/peek/overflow) y señales OR-eadas. |
| HOST-220 | [ui_bridge](220_ui_bridge/README.md) | Mini-SO: puente `os::Msg` → `ui::UiEvent` (ratón, teclado con modificadores, joystick/pad; descarta lo que no es entrada). |
| HOST-222 | [os_timer](222_os_timer/README.md) | Mini-SO: `TimerService` (frames/µs, one-shot/periódico, `stop`, capacidad). |
| HOST-236 | [os_latched](236_os_latched/README.md) | Mini-SO: prioridad (`PrioMsgQueue`, los `High` se cuelan), `peek`, coalescing de `MouseMove` y VBlank latched (secuencia + `missed`). |
| HOST-237 | [os_dispatch](237_os_dispatch/README.md) | Mini-SO: despacho por tabla (`HandlerTable`) indexada por `MsgType`, cobertura y `dispatch_all`. |
| HOST-238 | [os_time](238_os_time/README.md) | Mini-SO: `eng/os/time.hpp` — conversiones ticks↔µs (PAL/NTSC) y `ScopedTimer` con `TickSource`. |
| HOST-250 | [os_port](250_os_port/README.md) | Puerto de mensajes del mini-SO (`eng/os/port.hpp`): anillo SPSC (`push_isr`/`pop`/`peek`/`overflows`) y `MsgPort`. |
| HOST-251 | [reactive_loop](251_reactive_loop/README.md) | Bucle reactivo sobre `eng::App`: hook de VBlank del `Engine` → `MsgType::VBlank`, consumo en `update`, blit asíncrono → `MsgType::BlitDone`, `vblank_count`/`blitdone_count`, `pump`. |
| HOST-252 | [os_input](252_os_input/README.md) | Mini-SO: productores de entrada (`eng/os/input.hpp`) que emiten joystick/gamepad/ratón **solo al cambiar**, con posición clampada. |
| HOST-253 | [os_pump](253_os_pump/README.md) | Mini-SO: bucle reactivo `MessagePumpGame` (drena el puerto, `on_frame`/`on_render`). |
| HOST-256 | [os_keyboard](256_os_keyboard/README.md) | Mini-SO: teclado (`eng/os/input.hpp`) — bit-reverse del scancode de la CIA, down/up y modificadores. |
| HOST-257 | [os_stream](257_os_stream/README.md) | Mini-SO: `ChunkStream` (`eng/os/stream.hpp`) — doble buffer, underrun y EOF. |
| HOST-304 | [os_telemetry](304_os_telemetry/README.md) | Mini-SO: telemetría de saturación (`IrqTelemetry`: overflows/missed/marcas de agua). |
| HOST-305 | [os_tasks](305_os_tasks/README.md) | Mini-SO: tareas de fondo (`TaskSystem`: ciclo de vida, idle, preempt, prioridad). |
| HOST-306 | [os_file_stream](306_os_file_stream/README.md) | Mini-SO: feeder fichero→`ChunkStream` (lecturas secuenciales, EOF, underrun). |
| HOST-307 | [os_pump_tasks](307_os_pump_tasks/README.md) | Mini-SO: fondo integrado en el bucle (`MessagePumpGame` da idle solo sin mensajes; respeta/limpia `preempt`). |
| HOST-309 | [os_hook_pump_order](309_os_hook_pump_order/README.md) | Mini-SO: contrato de orden hook de VBlank→pump (`poll_and_post` + `MessagePumpGame::update` entregan el `Timer` en el mismo frame, periodo 1 y 2). |
