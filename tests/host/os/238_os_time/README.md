# HOST-238: tiempo y profiling (`eng::os`)

Test host de la capa de tiempo (`engine/include/eng/os/time.hpp`): conversiones ticks↔µs y
`ScopedTimer`.

## Qué comprueba

1. `ticks_to_us`/`us_to_ticks` con el reloj E del CIA: PAL (`709` kHz) y NTSC (`715` kHz).
2. `TickSource` + `ScopedTimer`: mide ticks transcurridos y su conversión a µs con una fuente falsa.

## Salida de referencia

```
OK: tiempo (ticks/us PAL-NTSC, ScopedTimer) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/238_os_time
```
