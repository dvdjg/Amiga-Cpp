# HOST-222: timers de usuario (`eng::os::TimerService`)

Test host de los timers de software del mini-SO (`engine/include/eng/os/timer.hpp`): frames/µs,
one-shot/periódico, `stop` y capacidad.

## Qué comprueba

1. **One-shot en frames**: no vence antes del deadline, postea `MsgType::Timer` con su id y termina.
2. **Periódico**: vence cada periodo y sigue activo; el id es estable.
3. **Microsegundos**: el deadline es `ticks_now + us_to_ticks(delay)`.
4. `stop` desactiva; se llenan `kMaxTimers` slots y el siguiente `start` devuelve 0.

## Salida de referencia

```
OK: timers de usuario (frames/us, periodico, stop, capacidad) validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/222_os_timer
```
