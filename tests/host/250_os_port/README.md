# HOST-250 — puerto de mensajes del mini-SO

Valida `engine/include/eng/os/port.hpp` (`eng::os::MsgQueue`/`MsgPort`), la cola de mensajes
del mini-SO que usa el bucle reactivo (y, en particular, la **notificación de fin de blit** de
`blitter_memcpy` asíncrona).

- **FIFO**: `post`/`try_get` conservan el orden.
- **Capacidad `N`**: caben `N-1` (una ranura de separación); `post` en cola llena → `false`.
- **Señal**: `signalled()` tras `post`; `clear`/`clear_signal` la quitan.

## Ejecución

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/250_os_port
```

Diseño canónico: `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`.
