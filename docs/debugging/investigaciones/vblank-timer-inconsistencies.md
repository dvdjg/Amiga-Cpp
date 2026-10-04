# Inconsistencias de VBlank y timers

## Estado

**Resuelto el núcleo (TIME-001..003, TIME-005..010)**; queda **TIME-004** abierto. `App::set_frame_sync`
elige la política de notificación con **una sola** secuencia de frame; `TimerService` es robusto ante
wrap, fase y catch-up, con handles generacionales; el backend entrega un reloj de µs real (CIA-B Timer
B). La resolución **sub-frame** de los timers µs sigue limitada por el sondeo por VBlank.

## Hallazgos

| ID | Severidad | Hallazgo | Evidencia | Consecuencia |
|---|---|---|---|---|
| TIME-001 | Alta | `App::on_vblank` publica un mensaje `VBlank` en `App::m_port`, cuya `PrioMsgQueue` conserva los VBlank en FIFO; `MsgPort` solo coalesce `MouseMove`. | `engine/include/eng/api/game.hpp` | **Resuelto**: `FrameSyncMode::Latch` (recomendado) no encola; `take_frame_tick` da la instantánea. `Event` conserva el FIFO histórico. |
| TIME-002 | Media | `App` mantiene un contador y un mensaje VBlank propios además del `VBlankLatch` global de `eng::os`. | `game.hpp`; `amiga_os.cpp` | **Resuelto**: `App::m_vblank_count` es la única secuencia; el modo decide la entrega. |
| TIME-003 | Alta | El backend Amiga llama `TimerService::poll_and_post(g_port, g_frame, 0u)`, así que no entrega ticks CIA. | `engine/src/platform/amiga/amiga_os.cpp` | **Resuelto**: se pasa `ciab_ticks_now()` (CIA-B Timer B continuo). |
| TIME-004 | Alta | Incluso con `ticks_now` correcto, los timers µs solo se comprueban desde `tick_body`, ligado al VBlank. | `amiga_os.cpp`; `timer.hpp` | **Abierto**: resolución efectiva de hasta un frame. Pendiente un pump sub-frame o one-shot de CIA. |
| TIME-005 | Media | Un timer periódico vencido se reprograma como `now + period`, no desde el deadline anterior. | `timer.hpp` | **Resuelto**: `deadline += period` (fase preservada) + política `Coalesce`/`SkipToNext`/`CatchUpAll`. |
| TIME-006 | Media | Comparación de vencimiento `now >= deadline` no es segura ante wrap de `u32`. | `timer.hpp` | **Resuelto**: `s32(now - deadline) >= 0` con horizonte < `2^31`. |
| TIME-007 | Media | `TimerService::start(id, ...)` permite ids duplicados; `stop(id)` detiene todas las coincidencias. | `timer.hpp` | **Resuelto**: `TimerHandle {slot, generation}`; `stop(handle)` cancela una instancia. |
| TIME-008 | Media | El payload de Timer solo lleva `id`; no expone deadline, expiraciones acumuladas ni coalescing. | `message.hpp` | **Resuelto**: `payload.timer = {id, handle, deadline, expirations}`. |
| TIME-009 | Alta | `Engine::run_frames()` puede observar un salto en `hb.frames` y ejecutar un solo `update/render`, descartando los ticks intermedios sin contabilizar `missed`. | `engine.hpp` | **Resuelto**: `context.frame.frames_elapsed` cuenta los latidos; el juego decide el catch-up. |
| TIME-010 | Diseño | No hay opción de arranque que exprese si se quiere IRQ de VBlank, espera activa/polling o ninguna notificación de frame. | `App::run()` | **Resuelto**: `App::set_frame_sync(FrameSyncMode::{Event,Latch,Disabled})`. |

## Estado útil ya implementado

- `VBlankLatch` mantiene secuencia monotónica, un único evento pendiente y contador `missed`.
- `MsgPayload::vblank` puede transportar `sequence` y `missed`.
- `os::frame_count()` da el contador del tick del mini-SO.
- `IrqTelemetry` acumula frames pisados y overflows.
- `Engine` soporta un bucle de fallback que espera VBlank desde el hilo principal.
- `TimerService` permite 16 timers one-shot/periódicos de frames o µs en una implementación pura
  host-testable.

Estas piezas no resuelven por sí solas los caminos indicados en TIME-001..TIME-010.

## Contrato recomendado de VBlank

Debe existir **una sola fuente de verdad** de secuencia de frame. La IRQ incrementa el contador
monotónico y actualiza un latch; el consumidor lee una instantánea coherente `{sequence, missed}`.
No debe encolarse un mensaje FIFO por cada VBlank salvo que el consumidor solicite explícitamente
semántica de evento por interrupción.

El productor debe contabilizar interrupciones reales aunque la aplicación no use mensajes. La
notificación y la espera se configuran por separado:

```cpp
enum class FrameSyncMode : u8 {
    VBlankLatch,     // IRQ + contador + latch coalescido
    ActiveWait,      // el juego llama wait_vblank() y no recibe mensajes VBlank
    External,        // el juego/host proporciona su propio reloj de frame
    Disabled         // sin tick ni hook automáticos
};
```

El API final puede diferir, pero debe permitir que el usuario elija política sin duplicar contadores.
`ActiveWait` no implica que el backend deje de medir el VBlank: solo desactiva el productor de
mensajes/hook que el juego no consume. `VBlankLatch` debe exponer también `frames_elapsed` o permitir
derivarlo mediante diferencia unsigned entre secuencias.

El consumidor define su política de catch-up:

- simulación fija: ejecutar hasta N pasos atrasados y reportar el excedente;
- juego no determinista: un update con `frames_elapsed` y animaciones basadas en tiempo acumulado;
- render latest-only: saltar render intermedio, pero conservar el tiempo lógico transcurrido.

## Contrato recomendado de timers

- Separar `FrameTimer` (resolución de frame) de `DeadlineTimer` monotónico (resolución según el
  servicio de tiempo). No anunciar precisión µs a un servicio que solo sondea una vez por VBlank.
- El servicio de µs debe consultar `TickClock::now()` desde un pump que corra con frecuencia
  suficiente, o programar un one-shot de CIA. La ISR solo registra vencimiento; el callback/mensaje
  se entrega fuera de IRQ salvo un servicio explícitamente IRQ-safe.
- Comparar deadlines con aritmética modular segura (`s32(now - deadline) >= 0`) y limitar el horizonte
  de deadline a menos de `2^31` ticks.
- Timers periódicos avanzan `deadline += period` para preservar fase. Si hay varios vencimientos, una
  política seleccionable define `CatchUpAll`, `Coalesce(count)` o `SkipToNext`; no se debe ocultar el
  atraso.
- Rechazar `delay == 0` para periódicos o definirlo como one-shot inmediato sin posibilidad de
  bucle infinito.
- El id debe ser único. `start` debe reemplazar explícitamente, fallar en duplicado o devolver un
  handle `{slot, generation}`; `stop` no debe cancelar accidentalmente varios timers reutilizados.
- El mensaje de expiración debe llevar al menos handle/generación, deadline o timestamp y número de
  periodos condensados.
- La cola llena debe tener política por timer: coalescing para periódicos, pero contabilización
  observable; no descartar silenciosamente one-shots críticos.

## Próximas pruebas necesarias

1. Publicar muchos VBlank sin consumir: un latch conserva la última secuencia y el conteo exacto de
   perdidos; el puerto de `App` no crece ni desborda.
2. Drenar el latch concurrentemente con el productor y verificar que no se pierde un VBlank entre
   lectura y limpieza de `pending`.
3. Simular frames/ticks que cruzan `UINT32_MAX`.
4. Verificar timers periódicos tras retraso de varios periodos: fase conservada y política de
   catch-up reportada.
5. Verificar timers µs con `ticks_now` que avanza sin VBlank y latencia máxima documentada.
6. Verificar ids duplicados, reemplazo, cancelación y mensajes tardíos tras reutilización de slot.
7. Probar los modos `VBlankLatch`, `ActiveWait`, `External` y `Disabled` sin instalar productores
   duplicados.

## Referencias

- [`MINI_OS_MESSAGE_LOOP.md`](../../engine/architecture/MINI_OS_MESSAGE_LOOP.md)
- [`MINI_OS_TIME.md`](../../engine/architecture/MINI_OS_TIME.md)
- [`ROADMAP_MINI_OS.md`](../../guides/roadmap/ROADMAP_MINI_OS.md)
- `engine/include/eng/os/port.hpp`
- `engine/include/eng/os/timer.hpp`
- `engine/include/eng/api/game.hpp`
- `engine/src/platform/amiga/amiga_os.cpp`
