# Referencia — `os/`

El **mini-SO** del engine (`eng::os`): un bucle **reactivo de mensajes** sobre el VBlank, sin preempción dura. La ISR (VBlank/entrada/disco/audio) **postea mensajes** en colas IRQ-safe; el hilo principal las **drena** por frame y despacha. Los productores de entrada y los timers se pollean una vez por VBlank; las **tareas de fondo** solo corren en **idle** y se abortan en cuanto llega algo importante. Ver `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`.

```
   ISR (VBlank/input/disco/audio)        hilo principal (por frame)
   ───────────────────────────           ───────────────────────────
   push_isr(mensaje) ──► MsgQueue SPSC ──► pump_messages() ──► handler(Msg)
   signal |= bit                          wait(mask) / tick()
   (nunca bloquea; si llena, descarta)    idle → TaskSystem (se aborta si preempt)
```

## Páginas

| Página | Qué documenta |
|---|---|
| [`messages.md`](messages.md) | `MsgType`/`Signal`/`Msg`, `MsgQueue`/`PrioMsgQueue`/`MsgPort`/`VBlankLatch`, `pump_messages`/`MessagePumpGame`, `HandlerTable`/`dispatch`. |
| [`core.md`](core.md) | Fachada `os.hpp` (`tick`/`wait`/`system_port`/`post_user`/`request_quit`), `time.hpp`, `TimerService`, `IrqTelemetry`. |
| [`io.md`](io.md) | `file.hpp` (E/S asíncrona + `IoUser`/`IoNotify`), `path.hpp`/`vfs.hpp` (VFS), `request.hpp` (generaciones), `stream.hpp`/`file_stream.hpp`, `trackdisk.hpp`/`floppy.hpp`. |
| [`tasks.md`](tasks.md) | `TaskSystem` (`TaskState`/`TaskId`/`TaskFn`), corrutinas de idle, `IntentDonePoster`. |
| [`input.md`](input.md) | Productores de entrada (`JoyProducer`/`PadProducer`/`MouseProducer`/`KeyProducer`), botones CD32. |

## Reglas

- **El `Msg` es un valor pequeño y copiable** (sin punteros propietarios): cabe en la cola y se copia en la ISR sin `variant` ni constructores virtuales (no son IRQ-aptos).
- **La ISR nunca bloquea**: si una cola se llena, el mensaje se descarta y `overflows` lo cuenta.
- **El frame y la entrada siempre ganan**: las tareas de fondo se abortan en cuanto la ISR marca `preempt`.
- El bucle es **cooperativo**: `wait(mask)` en el engine gira sobre `tick()` (ritmo de VBlank); en Workbench será `Wait(señales Exec)`.

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
