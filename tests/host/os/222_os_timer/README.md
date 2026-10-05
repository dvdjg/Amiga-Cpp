# HOST-222: timers de usuario (`eng::os::TimerService`)

Test host de los timers de software del mini-SO (`engine/include/eng/os/timer.hpp`): frames/µs,
one-shot/periódico, `stop` por handle, capacidad y las garantías de robustez resueltas en
`TIME-005..008` de
[vblank-timer-inconsistencies.md](../../../../docs/debugging/investigaciones/vblank-timer-inconsistencies.md).

## Qué comprueba

1. **One-shot en frames**: no vence antes del deadline, postea `MsgType::Timer` con id/handle/`expirations` y termina.
2. **Periódico**: vence cada periodo y sigue activo; el id es estable.
3. **Microsegundos**: el deadline es `ticks_now + us_to_ticks(delay)`.
4. **Handle generacional** (`TIME-007`): dos instancias en la misma ranura tienen handles distintos; `stop(handle)` cancela solo la instancia pedida; un handle obsoleto se rechaza; no se puede parar dos veces.
5. **Fase preservada** (`TIME-005`): un periódico avanza `deadline += period`, no `now + period`, así que la fase no acumula deriva aunque el sondeo llegue tarde.
6. **Catch-up** (`TIME-005`): `Coalesce` condensa el atraso en un mensaje con `expirations` = periodos; `SkipToNext` descarta el atraso y reprograma desde ahora; `CatchUpAll` entrega un mensaje por periodo vencido.
7. **Deadlines wrap-safe** (`TIME-006`): un deadline cerca del wrap de `u32` vence correctamente (`s32(now - deadline) >= 0`).
8. **Capacidad y rechazos**: se llenan `kMaxTimers` slots; sin slot libre el handle es inválido; un periódico con `delay == 0` se rechaza.

## Salida de referencia

```
OK: timers (frames/us, fase, wrap, catch-up, handles) validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/222_os_timer
```
