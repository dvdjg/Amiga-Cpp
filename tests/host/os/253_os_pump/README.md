# HOST-253: bucle reactivo del mini-SO (`MessagePumpGame`)

Test host del bucle reactivo (`engine/include/eng/os/message_pump.hpp`): drena el puerto y entrega
cada mensaje al `App` antes de la lógica de frame.

## Qué comprueba

1. `init` llama a `on_start`; `update` **drena el puerto** entregando cada mensaje a `on_msg`.
2. `update` llama a `on_frame` con el índice de frame; el puerto queda vacío.
3. `render` llama a `on_render`.
4. Sin puerto ligado, el bucle sigue (no falla) y no hay mensajes.

## Salida de referencia

```
OK: bucle reactivo (drena, on_frame, on_render) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/253_os_pump
```
