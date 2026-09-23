# HOST-305: tareas de fondo del mini-SO (M10)

Test host de `eng/os/task.hpp` (`TaskSystem`): tareas **cooperativas** que solo avanzan en **idle**
(cuando la cola principal está vacía) y se abortan cuando la ISR pide `preempt`.

## Qué comprueba

1. **Ciclo de vida**: `create` (id estable, `0` si no cabe) → `Created`; `start` → `Ready`;
   `suspend`/`resume`; `abort` → `Aborted`; `join` corre hasta `Finished`.
2. **Idle**: sin `start` no avanza; un slice por `run_idle`; una tarea `Blocked`/`Suspended` no
   corre.
3. **`preempt`**: `request_preempt` hace que `run_idle` se niegue a correr; `yield_if_preempt`
   (con el hook ligado) lo ve la tarea.
4. **`block`/`unblock`**.
5. **Prioridad**: con dos `Ready`, `run_idle` elige la de mayor prioridad; al terminar, corre la otra.

## Notas

- Núcleo de M10. **Pendiente**: `TaskMsgPort` propio (`own_port`) y `stack_words` (stack propio) para
  tareas con bloqueo real, y la integración en el bucle (`run_idle` cuando la cola principal está
  vacía).
- Sin heap y sin preempción dura: el `poll()` debe ceder en bucles largos con `yield_if_preempt`.

## Salida de referencia

```
OK: tareas de fondo del mini-SO (ciclo de vida + idle + preempt) validadas.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/305_os_tasks
```
