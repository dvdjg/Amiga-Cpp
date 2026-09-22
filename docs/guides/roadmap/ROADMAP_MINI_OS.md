# Roadmap del mini-SO de mensajes (`eng::os`)

Plan de implementación de la capa descrita en
[`docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`](../../engine/architecture/MINI_OS_MESSAGE_LOOP.md):
puerto de mensajes, productores de eventos y bucle de aplicación reactivo, más el puente a la
UI (`eng::ui`).

## Documentos de la familia

| Documento | Contenido |
|---|---|
| [MINI_OS_MESSAGE_LOOP.md](../../engine/architecture/MINI_OS_MESSAGE_LOOP.md) | Núcleo: `Msg`/`MsgPort`, señales, prioridad, VBlank latched y despacho por tabla. |
| [MINI_OS_INPUT.md](../../engine/architecture/MINI_OS_INPUT.md) | Registros de hardware → mensajes (ratón, joystick, CD32, teclado). |
| [MINI_OS_TIME.md](../../engine/architecture/MINI_OS_TIME.md) | Tiempo, timers de hardware, `TimerService` y profiling. |
| [MINI_OS_IO.md](../../engine/architecture/MINI_OS_IO.md) | E/S asíncrona y streaming desde disquete. |
| [GUI_LIBRARY.md](../../engine/architecture/GUI_LIBRARY.md) + [ROADMAP_GUI.md](ROADMAP_GUI.md) | Capa de UI (`eng::ui`) que consume los mensajes. |

## Principios

- **Componer, no duplicar.** El mini-SO se apoya en `eng::Engine` (VBlank interrupt-driven),
  `eng::task::BackgroundQueue` (trabajo diferido), `eng::input::InputAggregator` (estado de
  nivel), `eng::util::Event` (eventos intra-frame) y el servicio CIA ya existente
  (`install_timer_service`). Ver §2 del diseño.
- **Políticas de mensaje por tipo** (requisito, ver §10/§13 de `MINI_OS_MESSAGE_LOOP.md`): los
  eventos de flanco (`KeyDown/Up`, `MouseButton`, `FileDone`, `Timer` one-shot) son **FIFO** (no se
  pierden); el movimiento (`MouseMove`) se **coalesce** (gana el último, `push_mouse_coalesced`); el
  **VBlank** es **latched** (como máximo uno pendiente, con secuencia y `missed`); joystick/gamepad
  pueden ser latched (estado). La **prioridad** (`Low`/`Normal`/`High`) hace que los `High`
  (entrada/quit) **se cuelen** sobre la E-S en `pop`. `peek` permite mirar sin retirar.
- **HOST primero.** El núcleo (`Msg`, `MsgQueue`, `MsgPort`, prioridad, latched, servicios y
  puente) es puro y host-testable: se valida con tests HOST antes de tocar el backend Amiga.
- **Evidencia.** Cada fase cierra con su test HOST; la integración en hardware se valida con una
  demo (build → run → analyze) y el canal lateral, como el resto del repo.
- **Sin heap ni excepciones** en el camino caliente; capacidad fija y parámetros de plantilla.

## Fases

### M0 — Núcleo de mensajes

- **Entregable**: `eng/os/message.hpp` (`MsgType` contiguo, `Msg`, `MsgPayload`, `Signal`) y
  `eng/os/port.hpp` (`MsgQueue<N>` con `push_isr`/`pop`/`peek`, `MsgPort<N>` con
  `signal`/`take_signals`/`post`/`peek`).
- **Verificación**: **HOST-219** — anillo lleno/vacío, orden FIFO, `overflows()`, coalescing de
  señales, `take_signals` consume solo los bits pedidos, `Msg` trivialmente copiable
  (`static_assert`), `MsgType` con `switch` exhaustivo.
- **Estado**: **entregado** (`message.hpp` + `port.hpp`; HOST-219).

### M1 — VBlank latched (secuencia y frames perdidos)

- **Entregable**: contador de frames y **latch** de VBlank (`sequence` + `pending` + `missed`) en
  la IRQ de VBlank del backend; `os::frame_count()`; `take_vblank()`.
- **Integración**: reutiliza el `set_vblank_service` que ya usa `eng::Engine`; el VBlank **no**
  entra en la FIFO.
- **Verificación**: **HOST-236** (junto con M5) — la secuencia avanza aunque no se consuma, solo
  hay un VBlank pendiente y `missed` cuenta los pisados. Demo: contar frames por mensaje y
  compararlos con `context.frame.frame_index` (sin sondear `VPOSR`).
- **Estado**: **entregado** (`VBlankLatch`/`take_vblank` en `port.hpp`; HOST-236). El **productor**
  del backend es `eng::os::tick` (`amiga_minimal_os.cpp`): latcha el VBlank y pollea la entrada; lo
  llama el bucle por frame (demo 208). La variante **por IRQ** (`set_vblank_service`) queda como
  mejora cuando el bucle sea interrupt-driven.

### M2 — Entrada por registros → mensajes

- **Entregable**: productores de teclado (CIA-A serie, IRQ), ratón (`JOY0DAT` + CIA/POTINP),
  joystick (`JOY1DAT`) y pad CD32 (`POTGO`/`POTINP`) que emiten `KeyDown`/`KeyUp`/`MouseMove`/
  `MouseButton`/`Joystick`/`Gamepad`; snapshot de nivel en `InputAggregator`. Reutiliza
  `eng/platform/input_poll.hpp` (`decode_joy`) y `eng/input/input.hpp` (`PadState`).
- **Detalle**: [`MINI_OS_INPUT.md`](../../engine/architecture/MINI_OS_INPUT.md).
- **Verificación**: el decodificado puro ya está cubierto por HOST-006/HOST-007; se añade un test
  de que un cambio de registro produce **exactamente un mensaje** (y ninguno si no cambia).
- **Estado**: **casi entregado**. Entregado: los **productores puros** (`eng/os/input.hpp`:
  `JoyProducer`/`PadProducer`/`MouseProducer`/`KeyProducer` emiten solo al cambiar / por scancode;
  **HOST-252/256**), la **lectura de registros** en el backend (`eng::os::tick`: `JOYxDAT` +
  CIA-A PRA) y el **teclado** por IRQ de CIA-A serie (`os::enable_keyboard`, `SP` → `KeyDown`/`KeyUp`).
  Pendiente: verificar el teclado en hardware (el runner **no inyecta teclas**: no hay opción de
  inyección, solo `--automation-key`, que escribe un valor en memoria para el selector de técnicas).

### M3 — Puente a la UI

- **Entregable**: `eng/ui/event.hpp` (`UiEvent`, `UiEventKind`) y `eng/ui/ui_bridge.hpp`
  (`Msg` → `UiEvent`); `UiContext` (foco/widgets) es de la GUI (`ROADMAP_GUI.md`).
- **Verificación**: **HOST-220** — el puente traduce cada `MsgType` de entrada a su `UiEvent` y
  descarta los que no son de entrada; un `UiContext` de prueba recibe foco y despacha.
- **Estado**: **entregado** (`event.hpp` + `ui_bridge.hpp`; HOST-220). `UiContext` llega con la GUI.

### M4 — Bucle reactivo como `Game`

- **Entregable**: `MessagePumpGame<App>` que drena el puerto en `update`, saca el VBlank latched
  primero y despacha por tipo; patrón "esperar señales → drenar → lógica".
- **Verificación**: **demo 208_message_loop** — una escena mínima que reacciona a VBlank, a teclas
  y al ratón **sin leer hardware**; el gate visual comprueba que la escena cambia con la entrada y
  que el frame avanza.
- **Estado**: **entregado**. `eng/os/message_pump.hpp` (`MessagePumpGame<App>` drena el puerto y
  llama a `on_frame`/`on_render`; **HOST-253**) y la **demo 208_message_loop** (VBlank + input por
  mensajes; overlay con frames/mensajes/joystick). El bucle es de **polling** (`os::tick` por
  frame); el modo por IRQ queda como mejora.

### M5 — Prioridad, peek, coalescing y despacho

- **Entregable**: `MsgPrio` + `prio_of` + `PrioMsgQueue` (tres anillos, `pop` por prioridad,
  `peek`, `has_at_least`, `push_mouse_coalesced`); `wait(mask, timeout)`; `service_high_priority`;
  **tabla de handlers** `constexpr` indexada por `MsgType`.
- **Detalle**: §13–14 de [`MINI_OS_MESSAGE_LOOP.md`](../../engine/architecture/MINI_OS_MESSAGE_LOOP.md).
- **Verificación**: **HOST-236** (prioridad + coalescing + latched) y **HOST-237** (la tabla cubre
  todos los `MsgType` y despacha al handler correcto).
- **Estado**: **entregado** (`PrioMsgQueue`/`prio_of`/`wait` y `HandlerTable`/`dispatch_all`;
  HOST-236/237).

### M6 — Tiempo, timers y profiling

- **Entregable**: `eng/os/time.hpp` (`TickClock` sobre CIA-B, `ScopedTimer`, `beam_now`) y
  `eng/os/timer.hpp` (`TimerService` en frames/µs → `MsgType::Timer`); one-shot de CIA.
- **Detalle**: [`MINI_OS_TIME.md`](../../engine/architecture/MINI_OS_TIME.md).
- **Verificación**: **HOST-222** (timers de frames, periódicos y one-shot con ticks sintéticos) y
  **HOST-238** (TickClock: coherencia de lectura y conversión µs↔ticks).
- **Estado**: **entregado** (`time.hpp` + `timer.hpp`; HOST-222/238). La lectura del CIA
  (`TickClock`) la aporta el backend como `TickSource`.

### M7 — E/S asíncrona

- **Entregable**: `eng/os/file.hpp` (`FileHandle`, `file_open`/`read_async`/`write_async`/
  `close`, `IoNotify`) y `MsgType::FileDone`/`FileError`; backend `dos.library` (task auxiliar) y
  `trackdisk`; el decodificado se registra como tarea de `BackgroundQueue`.
- **Detalle**: [`MINI_OS_IO.md`](../../engine/architecture/MINI_OS_IO.md). La **caché de assets** y
  el **loader de código** que se construyen encima tienen su propio diseño
  ([`RESOURCE_SYSTEM.md`](../../engine/architecture/RESOURCE_SYSTEM.md)) y plan
  ([`ROADMAP_RESOURCES.md`](ROADMAP_RESOURCES.md)).
- **Verificación**: **HOST-221** — una E/S simulada (host) publica `FileDone` con el resultado y la
  señal `SigFile`; la decodificación diferida avanza por rebanadas.
- **Estado**: **casi entregado**. Entregado: el **contrato** `eng/os/file.hpp`, su implementación
  Amiga sobre **`dos.library`** (`amiga_minimal_file.cpp`: `Open`/`Read`/`Write`/`Seek`/`Close`,
  `CreateDir`/`DeleteFile`/`Rename`; la asíncrona como **diferida** con `file_pump` que postea
  `FileDone`/`FileError`), el **enrutado** `eng/res/resources.hpp` (HOST-255) y la **demo 211**
  (lee texto/imagen/sonido, carga un `.englib` y prueba la escritura). **Disquete a bajo nivel**
  (`eng/os/floppy.hpp` + `amiga_minimal_floppy.cpp`): DMA crudo (CIA-B PRB + `DSKPT`/`DSKLEN` doble +
  `DSKBLK`) y decode MFM en CPU — **sin `trackdisk.device`**. La DMA **funciona** en la demo
  `214_floppy_raw` (motor/seek/lectura de pista + syncs `$4489`); el decode del sector necesita
  búsqueda de sync **bit a bit** (pendiente). `eng/os/trackdisk.hpp` queda como alternativa
  documentada y no verificada (`td_open` se cuelga en `-nostdlib`). Decisión y detalle en
  `docs/debugging/investigaciones/consulta-grok-disco-y-loader.md`; errata CIA-B en `docs/reference/ahrm/ERRATA_Y_NOTAS.md` §5. **Imágenes de disquete**: `tools/fs/make-volume.mjs --adf` genera un
  ADF (FFS/OFS con `xdftool`) y `run-demo.sh --disk <adf>` lo monta en `DF0:`; el disquete se monta,
  pero **leer `df0:` desde una demo se bloquea** en el entorno sin Workbench (el volumen no queda
  montado), así que la lectura se prueba con el `DH1:` (demo 211).

### M8 — Streaming desde disquete

- **Entregable**: `AudioStream` (doble/triple buffer) que pide el siguiente chunk en cada
  `FileDone` y hace *swap* en la IRQ de audio; `file_read_async` en `MsgPrio::Low`.
- **Detalle**: §5 de [`MINI_OS_IO.md`](../../engine/architecture/MINI_OS_IO.md).
- **Verificación**: **HOST-239** — con E/S simulada, el stream llena N buffers, detecta *underrun*
  y termina en EOF sin perder chunks. Demo en hardware: audio continuo desde disquete.
- **Estado**: **parcial**. Entregado: la **máquina de estados** `eng/os/stream.hpp`
  (`ChunkStream<NumBuffers>`; **HOST-257**). Pendiente: la lectura real (`trackdisk`) y la demo.

### M9 — Telemetría

- **Entregable**: `overflows`, `missed` de VBlank y marcas de agua de la cola expuestas como
  `IrqTelemetry`; informe de saturación sin fallo silencioso.
- **Verificación**: test que inunda el puerto de E/S y comprueba que la entrada se drena antes y
  que la telemetría lo refleja.
- **Estado**: pendiente.

### M10 — Tareas asíncronas de fondo (`eng::os::TaskSystem`)

- **Entregable**: `eng/os/task.hpp` (`TaskState`, `TaskId`, `TaskMsgPort`, `TaskDesc`, `TaskSystem`)
  con ciclo de vida (`create`/`start`/`suspend`/`resume`/`abort`/`join`), scheduler de **idle**
  (`run_idle`), `request_preempt`/`yield_if_preempt` y `wait_or_idle`.
- **Detalle**: [`MINI_OS_TASKS.md`](../../engine/architecture/MINI_OS_TASKS.md).
- **Verificación**: **HOST-250** — una tarea `poll()` avanza por rebanadas solo en idle; al marcar
  preempt (simulado) sale y vuelve a `Ready` sin perder el progreso; `suspend`/`resume`/`abort` y
  `TaskFinished` se comportan; `wait_or_idle` drena la cola principal antes que el fondo.
- **Estado**: pendiente.

### M11 — Tareas-corrutina (opcional)

- **Entregable**: tareas con `co_await idle_yield{}`/`co_await wait_for_signal{}` sobre el mismo
  `TaskSystem` (frame en buffer fijo, sin heap).
- **Detalle**: §8 de [`MINI_OS_TASKS.md`](../../engine/architecture/MINI_OS_TASKS.md).
- **Verificación**: **HOST-251** — una corrutina cede y reanuda conservando estado; medición de
  codegen en 68000 (sin libcalls) y comparación con la tarea `poll()`.
- **Estado**: pendiente.

## Tests y demos previstos

| ID | Tipo | Contenido |
|---|---|---|
| HOST-219 | test | Núcleo: `MsgQueue`/`MsgPort`/señales/coalescing. |
| HOST-220 | test | Puente `Msg` → `UiEvent` y `UiContext` mínimo. |
| HOST-221 | test | E/S asíncrona simulada + decodificado diferido por tareas. |
| HOST-222 | test | Timers de usuario (frames/µs) y mensajes de aplicación. |
| HOST-236 | test | Prioridad, `peek`, coalescing y VBlank latched. |
| HOST-237 | test | Despacho por tabla: cobertura de todos los `MsgType`. |
| HOST-238 | test | `TickClock` (coherencia y conversión µs↔ticks) y `beam_now`. |
| HOST-239 | test | Streaming (doble buffer, underrun, EOF) con E/S simulada. |
| HOST-250 | test | Tareas de fondo: idle/preempt, ciclo de vida y `wait_or_idle`. |
| HOST-251 | test | Tareas-corrutina (`co_await idle_yield`) y codegen 68000. |
| 208_message_loop | demo | Bucle reactivo en hardware: VBlank + input + UI sin sondeo. |
| 209_audio_stream | demo | Audio continuo desde disquete con `AudioStream`. |

## Riesgos y decisiones abiertas

- **IRQ vs soft-int para el input.** La entrada por flanco de CIA es más limpia, pero comparte el
  autovector de nivel 2 con el timer de fondo; la alternativa es un soft-int tras el sondeo de
  `potgo`, con coste por frame.
- **Orden de bits del CD32.** El bitmask del pad se calibra una vez contra hardware/WinUAE; el
  diseño fija el mecanismo, no el orden.
- **Alineación del payload.** La unión de `Msg` debe quedar alineada a `u32` para que la copia en
  la ISR sea barata; el test M0 lo fija con `static_assert`.
- **CIA-A vs CIA-B.** El timer de fondo usa CIA-A Timer A; el reloj libre de µs conviene en CIA-B
  para no colisionar ni con el fondo ni con el teclado.
- **Puertos por subsistema.** Antes de añadir un *lock* o un anillo multi-productor, preferir un
  puerto propio por productor.
- **Compatibilidad con Exec.** Si el juego corriera bajo un SO real, el backend debe poder mapear a
  `Wait`/`GetMsg`/`DoIO`; conviene no cerrar esa puerta en la API pública.

## Estado

Todas las fases están **pendientes**. La estructura de código (`engine/include/eng/os/`,
`engine/include/eng/ui/`) y el diseño están fijados; la implementación empieza por M0.
