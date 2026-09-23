# HOST-219: núcleo del mini-SO (`eng::os`)

Test host del núcleo del mini-SO (`engine/include/eng/os/message.hpp` + `port.hpp`): vocabulario de
mensajes y puerto/cola IRQ-safe.

## Qué comprueba

1. `Msg` es **trivialmente copiable** y `MsgType` es **contiguo desde 0** (índice de tabla).
2. `MsgQueue<N>`: FIFO, capacidad útil `N-1`, `overflows()` cuando está llena.
3. `MsgPort`: señales OR-eadas (`SigVBlank`/`SigInput`/`SigHigh`), `take_signals` selectivo y
   coalescing de señales (dos mensajes, una señal).
4. `prio_of`/`signal_for`: prioridad y señal por tipo.

## Salida de referencia

```
OK: mini-SO nucleo (Msg, cola SPSC, senales, prioridad) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/219_os_core
```
