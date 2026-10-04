# HOST-415: política de sincronización de frame y catch-up

Test host del contrato de VBlank único y las políticas de notificación
(`engine/include/eng/os/port.hpp`): resuelve `TIME-001/002/009/010` de
[vblank-timer-inconsistencies.md](../../../../docs/debugging/investigaciones/vblank-timer-inconsistencies.md).

## Qué comprueba

1. **Una sola fuente de secuencia**: el contador avanza igual en los tres modos; no hay segundo
   contador que pueda divergir.
2. **Modo `Event`**: un mensaje `MsgType::VBlank` FIFO por latido (comportamiento histórico).
3. **Modo `Latch`**: no encola; `take_vblank` entrega la instantánea `{sequence, missed}` y
   `VBlankTick::frames_elapsed(from)` deriva el catch-up con aritmética unsigned.
4. **Modo `Disabled`**: el contador sigue avanzando (el backend mide igual el VBlank) pero no se
   notifica nada; para juegos que gestionan su propio sincronismo.
5. **Wrap**: `frames_elapsed` atraviesa el wrap de `u32`.

El `App` real no es host-testable (necesita backend), así que el test modela su despacho de
`on_vblank` por modo sobre un `MsgPort`/`VBlankLatch` reales.

## Salida de referencia

```
OK: politica de frame sync (Event/Latch/Disabled) y catch-up validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/415_frame_sync_policy
```
