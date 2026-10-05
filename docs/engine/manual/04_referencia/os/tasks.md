# Referencia — tareas de fondo

`eng::os::TaskSystem` (`os/task.hpp:64`) son tareas **cooperativas** con identidad, estado y función `poll()` que avanza un poco y vuelve. **No compiten** con el bucle de mensajes: solo corren en **idle** (cuando la cola principal está vacía) y se **abortan** en cuanto la ISR marca `preempt` (VBlank/input/disco). Así el frame y la entrada siempre ganan. Sin heap, sin excepciones y sin preempción dura.

```
   bucle principal (por frame)              TaskSystem (solo en idle)
   ───────────────────────────              ────────────────────────
   drena puerto ──► handlers                elige una Ready por prioridad
   ¿cola vacía? ──► run_idle(budget) ─────► poll(slice_us) ──► ¿preempt? aborta
   VBlank/input ──► preempt = true          Running → Ready (o Finished si false)
```

## Tipos

| Tipo | Qué es |
|---|---|
| `TaskState` (`task.hpp:24`) | `Invalid`/`Created`/`Ready`/`Running`/`Blocked`/`Suspended`/`Finished`/`Aborted`. |
| `TaskId` (`:36`) | Identificador (`0` = inválido). |
| `TaskFn` (`:40`) | `bool(*)(TaskId self, void* user, u32 slice_us)`: `true` = sigue viva; `slice_us` es **orientativo**. |
| `TaskMsgPort` (`:44`) | Cola propia de una tarea (`PrioMsgQueue<16>`): la tarea drena **su** puerto (nunca el del SO). |
| `TaskDesc` (`:54`) | Descripción de una tarea. |

## Comportamiento

El `TaskSystem` elige una tarea `Ready` por prioridad (round-robin entre iguales), la marca `Running`, llama a su `poll()` con un **presupuesto** (`slice_us`) y, si al volver sigue `Running`, la deja `Ready` (o `Finished` si `poll()` devolvió `false`). `wait_or_idle(main, tasks, mask, ...)` (`task.hpp:337`) es el bucle que espera señales o, si no hay, corre tareas de fondo. Las corrutinas (`co_await idle_yield`) son opcionales. Ver `docs/engine/architecture/MINI_OS_TASKS.md` §1–2.

## Completación de intenciones — `os/intent_done.hpp`

`IntentDonePoster<N>` (`intent_done.hpp:24`) es la política `Done` **real** de una `IntentQueue`: cuando una intención alcanza su punto, postea `MsgType::IntentDone` (el `ticket` viaja en `payload.user.a`).

Volver al [índice de `os/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
