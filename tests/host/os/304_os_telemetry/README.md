# HOST-304: telemetría de saturación del mini-SO (M9)

Test host de `eng/os/telemetry.hpp` (`IrqTelemetry`): expone los contadores de saturación del
mini-SO **sin fallo silencioso**.

## Qué comprueba

1. **Descartes por cola llena**: al encolar sobre el anillo `High` (capacidad `N-1`) sube
   `queue_overflows`, y `sample` no re-cuenta el mismo descarte (acumula el **delta** de
   `overflows()`).
2. **VBlank pisados**: señales de VBlank sin consumir → `vblank_missed`; `take_vblank` entrega el
   `Msg` con `sequence`/`missed` y limpia el latch (no re-cuenta).
3. **Marcas de agua**: `peak_depth`/`peak_high` reflejan la profundidad máxima observada
   (`PrioMsgQueue::depth`/`depth_total`).
4. **`note_vblank`/`observe`/`reset`/`saturated`**.

## Notas

- `sample` se llama **una vez por frame** y **antes** de `take_vblank` (para capturar `missed`); si
  se consume el latch primero, usar `note_vblank(missed)` con el payload del mensaje.
- `depth()`/`depth_total()` se añadieron a `MsgQueue`/`PrioMsgQueue` para esta telemetría.

## Salida de referencia

```
OK: telemetria del mini-SO (overflows/missed/marcas de agua) validada.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/304_os_telemetry
```
