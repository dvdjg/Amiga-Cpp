# Roadmap del mini-SO de mensajes (`eng::os`)

Plan de implementación de la capa descrita en
[`docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`](../../engine/architecture/MINI_OS_MESSAGE_LOOP.md):
puerto de mensajes, productores de eventos y bucle de aplicación reactivo, más el puente a la
UI (`eng::ui`).

## Principios

- **Componer, no duplicar.** El mini-SO se apoya en `eng::Engine` (VBlank interrupt-driven),
  `eng::task::BackgroundQueue` (trabajo diferido), `eng::input::InputAggregator` (estado de
  nivel) y `eng::util::Event` (eventos intra-frame). Ver §2 del diseño.
- **HOST primero.** El núcleo (`Msg`, `MsgQueue`, `MsgPort`, servicios y puente) es puro y
  host-testable: se valida con tests HOST antes de tocar el backend Amiga.
- **Evidencia.** Cada fase cierra con su test HOST; la integración en hardware se valida con una
  demo (build → run → analyze) y el canal lateral, como el resto del repo.
- **Sin heap ni excepciones** en el camino caliente; capacidad fija y parámetros de plantilla.

## Fases

### M0 — Núcleo de mensajes (hecho cuando pase HOST)

- **Entregable**: `eng/os/message.hpp` (`MsgType`, `Msg`, `MsgPayload`, `Signal`) y
  `eng/os/port.hpp` (`MsgQueue<N>`, `MsgPort<N>` con `signal`/`take_signals`/`wait`).
- **Verificación**: **HOST-219** — anillo lleno/vacío, orden FIFO, `overflows()`, coalescing de
  señales (dos mensajes, una señal), `take_signals` consume solo los bits pedidos, `Msg` es
  trivialmente copiable (`static_assert`), `MsgType` con `switch` exhaustivo.
- **Estado**: pendiente.

### M1 — Productor de VBlank

- **Entregable**: contador de frames y emisión de `MsgType::VBlank` desde la IRQ de VBlank del
  backend Amiga; `os::frame_count()`; coalescing (`flags` de frame atrasado) si procede.
- **Integración**: reutiliza el `set_vblank_service` que ya usa `eng::Engine`.
- **Verificación**: demo de arranque que cuente frames por mensaje y los compare con
  `context.frame.frame_index` (sin sondeo de `VPOSR`).
- **Estado**: pendiente.

### M2 — Entrada por mensajes y puente a la UI

- **Entregable**: productores de teclado/ratón/joystick (CIA/potgo/joyport) que emiten
  `KeyDown`/`KeyUp`/`MouseMove`/`MouseButton`/`Joystick`; snapshot de nivel en
  `InputAggregator`; `eng/ui/ui_context.hpp` (`UiEvent`, `UiContext`) y
  `eng/ui/ui_bridge.hpp` (`Msg` → `UiEvent`).
- **Verificación**: **HOST-220** — el puente traduce cada `MsgType` de entrada a su `UiEvent`
  (posición, botón, tecla, modificadores) y descarta los tipos que no son de entrada; un
  `UiContext` de prueba recibe foco y despacha.
- **Estado**: pendiente.

### M3 — Bucle reactivo como `Game`

- **Entregable**: `MessagePumpGame<App>` (o un adaptador equivalente) que drena el puerto en
  `update`, despacha por tipo y llama a `on_frame`; patrón de "esperar señales → drenar →
  lógica".
- **Verificación**: **demo 206_message_loop** — una escena mínima que reacciona a VBlank, a
  teclas y al ratón **sin leer hardware**; el gate visual comprueba que la escena cambia con la
  entrada y que el frame avanza.
- **Estado**: pendiente.

### M4 — E/S asíncrona de disco

- **Entregable**: `eng/os/file.hpp` (`FileHandle`, `file_open`/`read_async`/`write_async`/
  `close`) y `MsgType::FileDone`/`FileError`; el decodificado se registra como tarea de
  `BackgroundQueue`.
- **Verificación**: **HOST-221** — una E/S simulada (host) publica `FileDone` con el resultado y
  la señal `SigFile`; la decodificación diferida avanza por rebanadas. En hardware, demo que
  carga un recurso y muestra el progreso por mensaje.
- **Estado**: pendiente.

### M5 — Timers de usuario y mensajes de aplicación

- **Entregable**: `os::add_timer(id, frames)` → `MsgType::Timer`; `os::post_user(code, a, b)` →
  `MsgType::User`; `os::request_quit()` → `MsgType::Quit`.
- **Verificación**: **HOST-222** — un timer de `N` frames emite a los `N` (contando ticks
  sintéticos); `post_user` desde otro subsistema llega en orden.
- **Estado**: pendiente.

### M6 — Prioridad y telemetría

- **Entregable**: decisión de **varios puertos** (`input_port`, `io_port`) frente a un anillo
  con prioridad por rango de `MsgType`; telemetría (`overflows`, frames perdidos) accesible
  como `IrqTelemetry`.
- **Verificación**: test que inunda el puerto de E/S y comprueba que la entrada se drena antes;
  informe de saturación sin fallo silencioso.
- **Estado**: pendiente.

## Tests y demos previstos

| ID | Tipo | Contenido |
|---|---|---|
| HOST-219 | test | Núcleo: `MsgQueue`/`MsgPort`/señales/coalescing. |
| HOST-220 | test | Puente `Msg` → `UiEvent` y `UiContext` mínimo. |
| HOST-221 | test | E/S asíncrona simulada + decodificado diferido por tareas. |
| HOST-222 | test | Timers y mensajes de aplicación. |
| 206_message_loop | demo | Bucle reactivo en hardware: VBlank + input + UI sin sondeo. |

## Riesgos y decisiones abiertas

- **Máscara de señales y pérdida de VBlanks.** Un solo bit no distingue "un frame" de "dos
  frames atrasados": hay que decidir entre coalescer (contar perdidos) o encolar varios; el
  diseño propone coalescer con contador.
- **IRQ vs soft-int para el input.** La entrada por flanco de CIA es más limpia, pero exige
  gestionar el vector con cuidado (convivencia con el VBlank); la alternativa es un soft-int
  lanzado tras el sondeo de `potgo`, con coste por frame.
- **Alineación del payload.** La unión de `Msg` debe quedar alineada a `u32` para que la copia
  en la ISR sea barata; el test M0 lo fija con `static_assert`.
- **Puertos por subsistema.** Antes de añadir un *lock* o un anillo multi-productor, preferir un
  puerto propio por productor.
- **Compatibilidad con Exec.** Si el juego corriera bajo un SO real, el backend debe poder
  mapear a `Wait`/`GetMsg`/`DoIO`; conviene no cerrar esa puerta en la API pública.

## Estado

Todas las fases están **pendientes**. La estructura de código (`engine/include/eng/os/`,
`engine/include/eng/ui/`) y el diseño están fijados; la implementación empieza por M0.
